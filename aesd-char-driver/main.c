/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/fs.h>

#include "aesdchar.h"
#include "aesd_ioctl.h"

int aesd_major = 0;
int aesd_minor = 0;

MODULE_AUTHOR("Ajay Kandagal");
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    struct aesd_dev *dev;

    PDEBUG("open");

    if (filp->private_data == NULL)
    {
        dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
        filp->private_data = dev;
    }

    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    /**
     * TODO: handle release
     */
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                  loff_t *f_pos)
{
    ssize_t retval = 0;
    ssize_t offset = 0;
    size_t act_count;
    size_t rem_count = count;
    struct aesd_buffer_entry *entryptr;
    struct aesd_dev *dev = filp->private_data;

    PDEBUG("read %zu bytes with offset %lld", count, *f_pos);

    /**
     * TODO: handle read
     */
    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;

    /* Loop to read "count" number of bytes from all the entires of
    circular buffer */
    do
    {
        entryptr = aesd_circular_buffer_find_entry_offset_for_fpos(
            &dev->cb_buffer,
            *f_pos, &offset);

        if (entryptr == NULL)
            goto out;

        // Number of bytes to read from current entry buffer
        act_count = (entryptr->size - offset);

        // Cannot read more than requested byte count
        if (act_count > rem_count)
            act_count = rem_count;

        if (copy_to_user((void *)buf + (count - rem_count),
                         (void *)entryptr->buffptr + offset, act_count))
        {
            retval = -EFAULT;
            goto out;
        }

        rem_count -= act_count;

        *f_pos += act_count;
        retval += act_count;

    } while (rem_count);

out:
    mutex_unlock(&dev->lock);
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                   loff_t *f_pos)
{
    struct aesd_dev *dev = filp->private_data;
    ssize_t retval = 0;

    PDEBUG("write %zu bytes with offset %lld", count, *f_pos);

    /**
     * TODO: handle write
     */
    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;

    if (dev->entryptr.buffptr)
        dev->entryptr.buffptr = (char *)krealloc(dev->entryptr.buffptr,
                                                 dev->entryptr.size + count, GFP_KERNEL);
    else
        dev->entryptr.buffptr = (char *)kmalloc(sizeof(char) * count, GFP_KERNEL);

    if (dev->entryptr.buffptr == NULL)
    {
        PDEBUG("Error while allocating memmory to buffer\n");
        retval = -ENOMEM;
        goto out;
    }

    if (copy_from_user((void *)dev->entryptr.buffptr + dev->entryptr.size, buf, count))
    {
        retval = -EFAULT;
        goto out;
    }

    dev->entryptr.size += count;
    retval = count;

    // If '\n' is present then add the entry to the circular buffer
    if (memchr(dev->entryptr.buffptr, '\n', dev->entryptr.size))
    {
        // Free the buffer which was previously added at "in_offs" index
        if (dev->cb_buffer.entry[dev->cb_buffer.in_offs].buffptr)
        {
            kfree(dev->cb_buffer.entry[dev->cb_buffer.in_offs].buffptr);
            dev->total_bytes -= dev->cb_buffer.entry[dev->cb_buffer.in_offs].size;
        }

        aesd_circular_buffer_add_entry(&dev->cb_buffer, &dev->entryptr);
        dev->total_bytes += dev->entryptr.size;
        dev->entryptr.buffptr = NULL;
        dev->entryptr.size = 0;
    }

out:
    mutex_unlock(&dev->lock);
    return retval;
}

/**
 * Reference taken from scull driver
 */
loff_t aesd_llseek(struct file *filp, loff_t off, int whence)
{
    loff_t newpos;
    struct aesd_dev *dev = filp->private_data;

    switch (whence)
    {
    case 0: /* SEEK_SET */
        newpos = off;
        break;

    case 1: /* SEEK_CUR */
        newpos = filp->f_pos + off;
        break;

    case 2: /* SEEK_END */
        if (dev->total_bytes >= off)
            newpos = dev->total_bytes - off;
        else
            return -EINVAL;
        break;

    default: /* can't happen */
        return -EINVAL;
    }

    if (newpos < 0)
        return -EINVAL;

    filp->f_pos = newpos;
    return newpos;
}

/*******************************************************************************
 * @brief   Calculates filp position value taking write_cmd and write_cmd_offset
 * values.
 *
 * @param   write_cmd Value represents  the command to seek into zero referenced
 * number of commands currently stored by the driver in the command circular
 * buffer.
 * @param   write_cmd_offset Value represents the zero referenced offset within
 *          the command to seek into.
 *
 * @return  Returns zero on success elese error value.
 *******************************************************************************/
static long aesd_adjust_file_offset(struct file *filp, unsigned int write_cmd,
                                    unsigned int write_cmd_offset)
{
    struct aesd_dev *dev = filp->private_data;
    int retval = 0, cb_size = 0, tmp_fpos = 0, i = 0;

    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;

    // Get circular buffer size
    if (dev->cb_buffer.in_offs == dev->cb_buffer.out_offs)
        cb_size = dev->cb_buffer.full ? AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED : 0;
    else if (dev->cb_buffer.in_offs < dev->cb_buffer.out_offs)
        cb_size = AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED - dev->cb_buffer.out_offs +
                  dev->cb_buffer.in_offs;
    else
        cb_size = dev->cb_buffer.in_offs - dev->cb_buffer.out_offs;

    if (write_cmd >= cb_size)
    {
        retval = -EINVAL;
        goto out;
    }

    if (write_cmd_offset >= dev->cb_buffer.entry[write_cmd].size)
    {
        retval = -EINVAL;
        goto out;
    }

    // Calculate sum of all the command buffers size till write_cmd index
    for (i = 0; i < write_cmd; i++)
        tmp_fpos += dev->cb_buffer.entry[i].size;

    // Add offset
    tmp_fpos += write_cmd_offset;

    filp->f_pos = tmp_fpos;

out:
    mutex_unlock(&dev->lock);
    return retval;
}

/**
 * Taken reference from scull driver code.
 */
long aesd_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    struct aesd_seekto seekto;
    int retval = 0;

    /*
     * extract the type and number bitfields, and don't decode
     * wrong cmds: return ENOTTY (inappropriate ioctl) before access_ok()
     */
    if (_IOC_TYPE(cmd) != AESD_IOC_MAGIC)
        return -ENOTTY;
    if (_IOC_NR(cmd) > AESDCHAR_IOC_MAXNR)
        return -ENOTTY;

    switch (cmd)
    {
    case AESDCHAR_IOCSEEKTO:
        if (copy_from_user(&seekto, (const void __user *)arg, sizeof(seekto)) != 0)
        {
            retval = -EFAULT;
        }
        else
        {
            retval = aesd_adjust_file_offset(filp, seekto.write_cmd,
                                             seekto.write_cmd_offset);
        }
        break;

    /* Redundant as cmd was checked against MAXNR, but the error can be thrown for
       unhandled cases */
    default:
        return -ENOTTY;
    }

    return retval;
}

struct file_operations aesd_fops = {
    .owner = THIS_MODULE,
    .read = aesd_read,
    .write = aesd_write,
    .open = aesd_open,
    .release = aesd_release,
    .llseek = aesd_llseek,
    .unlocked_ioctl = aesd_ioctl};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add(&dev->cdev, devno, 1);

    if (err)
        printk(KERN_ERR "Error %d adding aesd cdev", err);

    return err;
}

int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;

    result = alloc_chrdev_region(&dev, aesd_minor, 1, "aesdchar");
    aesd_major = MAJOR(dev);

    if (result < 0)
    {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }

    memset(&aesd_device, 0, sizeof(struct aesd_dev));

    /**
     * TODO: initialize the AESD specific portion of the device
     */
    aesd_circular_buffer_init(&aesd_device.cb_buffer);
    mutex_init(&aesd_device.lock);

    result = aesd_setup_cdev(&aesd_device);

    if (result)
    {
        unregister_chrdev_region(dev, 1);
    }
    return result;
}

void aesd_cleanup_module(void)
{
    struct aesd_buffer_entry *entryptr;
    uint8_t index = 0;

    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    /**
     * TODO: cleanup AESD specific poritions here as necessary
     */
    AESD_CIRCULAR_BUFFER_FOREACH(entryptr, &aesd_device.cb_buffer, index)
    {
        if (entryptr->buffptr)
            kfree(entryptr->buffptr);
    }
    mutex_destroy(&aesd_device.lock);

    unregister_chrdev_region(devno, 1);
}

module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
