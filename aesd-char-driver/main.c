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
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/fs.h> // file_operations
#include "aesdchar.h"

int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Your Name Here"); /** TODO: fill in your name **/
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    struct aesd_dev *dev;

    PDEBUG("open");
    /**
     * TODO: handle open
     */
    if (filp->private_data == NULL) {
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
    struct aesd_dev *dev = filp->private_data;
    struct aesd_buffer_entry *entryptr;
    ssize_t retval = 0;
    ssize_t offset = 0;
    size_t rem_count = count, act_count;
    
    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);

    /**
     * TODO: handle read
     */
    if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;

    do {
        entryptr = aesd_circular_buffer_find_entry_offset_for_fpos(&dev->cb_buffer, *f_pos, &offset);

        if (entryptr == NULL)
            goto out;

        // Only read till available read bytes
        act_count = (entryptr->size - offset);

        if (act_count > rem_count)
            act_count = rem_count;

        if (copy_to_user((void*)buf + (count - rem_count), (void*)entryptr->buffptr + offset, act_count)) {
            retval = -EFAULT;
            goto out;
        }

        rem_count -= act_count;

        *f_pos += act_count;
        retval += act_count;

    } while(rem_count);

    out:
    mutex_unlock(&dev->lock);
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    struct aesd_dev *dev = filp->private_data;
    ssize_t retval = 0;

    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);
    /**
     * TODO: handle write
     */
    if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;

    if (dev->entryptr.buffptr)
        dev->entryptr.buffptr = (char *)krealloc(dev->entryptr.buffptr,  dev->entryptr.size + count, GFP_KERNEL);
    else
        dev->entryptr.buffptr = (char *)kmalloc(sizeof(char) * count, GFP_KERNEL);
        
    if (dev->entryptr.buffptr == NULL)
    {
        PDEBUG("Error while allocating memmory to buffer\n");
        retval = -ENOMEM;
        goto out;
    }

    // copy data including \n and update malloc buffer length
    if (copy_from_user((void*)dev->entryptr.buffptr + dev->entryptr.size, buf, count)) {
		retval = -EFAULT;
		goto out;
	}
	// *f_pos += count;
    dev->entryptr.size += count;

    if (strchr(dev->entryptr.buffptr, '\n')) {
        if (dev->cb_buffer.entry[dev->cb_buffer.in_offs].buffptr)
            kfree(dev->cb_buffer.entry[dev->cb_buffer.in_offs].buffptr);

        aesd_circular_buffer_add_entry(&dev->cb_buffer, &dev->entryptr);
        dev->entryptr.buffptr = NULL;
        dev->entryptr.size = 0;
    }

	retval = count;

    out:
	mutex_unlock(&dev->lock);
	return retval;
}

loff_t scull_llseek(struct file *filp, loff_t off, int whence)
{
	// struct aesd_dev *dev = filp->private_data;
	loff_t newpos;

	switch(whence) {
	  case 0: /* SEEK_SET */
		newpos = off;
		break;

	  case 1: /* SEEK_CUR */
		newpos = filp->f_pos + off;
		break;

	//   case 2: /* SEEK_END */
	// 	newpos = dev->size + off;
	// 	break;

	  default: /* can't happen */
		return -EINVAL;
	}

	if (newpos < 0) 
        return -EINVAL;

	filp->f_pos = newpos;
	return newpos;
}

struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
    .llseek =  scull_llseek,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add (&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }

    return err;
}



int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1,
            "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device,0,sizeof(struct aesd_dev));

    /**
     * TODO: initialize the AESD specific portion of the device
     */
    aesd_circular_buffer_init(&aesd_device.cb_buffer);
    mutex_init(&aesd_device.lock);

    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
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
    AESD_CIRCULAR_BUFFER_FOREACH(entryptr, &aesd_device.cb_buffer, index) {
        if (entryptr->buffptr)
            kfree(entryptr->buffptr);
    }

    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
