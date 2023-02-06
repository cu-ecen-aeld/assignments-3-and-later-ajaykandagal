#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.

set -e
set -u

OUTDIR=/tmp/aeld
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.1.10
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath $(dirname $0))
ARCH=arm64
CROSS_COMPILE=aarch64-none-linux-gnu-
SYSROOT=$(${CROSS_COMPILE}gcc -print-sysroot)

if [ $# -lt 1 ]
then
	echo "Using default directory ${OUTDIR} for output"
else
	OUTDIR=$1
	echo "Using passed directory ${OUTDIR} for output"
fi

mkdir -p ${OUTDIR}

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/linux-stable" ]; then
    #Clone only if the repository does not exist.
	echo "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR}"
	git clone ${KERNEL_REPO} --depth 1 --single-branch --branch ${KERNEL_VERSION}
fi
if [ ! -e ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ]; then
    cd linux-stable
    echo "Checking out version ${KERNEL_VERSION}"
    git checkout ${KERNEL_VERSION}

    # TODO: Add your kernel build steps here
    make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE mrproper
    make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE defconfig
    make -j$(nproc) ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE all
    make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE modules
    make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE dtbs
fi

echo "Adding the Image in outdir"

echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]
then
	echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm  -rf $OUTDIR/rootfs
fi

echo "Creating necessary base directories"
# TODO: Create necessary base directories
mkdir -p $OUTDIR/rootfs && cd $OUTDIR/rootfs
mkdir -p bin dev etc home lib lib64 proc sbin sys tmp usr var
mkdir -p usr/bin usr/lib usr/bin
mkdir -p var/log

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/busybox" ]
then
git clone git://busybox.net/busybox.git
    cd busybox
    git checkout ${BUSYBOX_VERSION}
    # TODO:  Configure busybox
    make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE distclean
    make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE defconfig
else
    cd busybox
fi

echo "Running make and install for busybox"
# TODO: Make and install busybox
make -j$(nproc) ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE
make CONFIG_PREFIX=$OUTDIR/rootfs ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE install

cd $OUTDIR/rootfs
echo "Library dependencies"
${CROSS_COMPILE}readelf -a bin/busybox | grep "program interpreter"
${CROSS_COMPILE}readelf -a bin/busybox | grep "Shared library"

echo "Adding library dependencies to rootfs"
# TODO: Add library dependencies to rootfs
cp $SYSROOT/lib/ld-linux-aarch64.so.1 lib/
cp $SYSROOT/lib64/libm.so.6 lib64/
cp $SYSROOT/lib64/libresolv.so.2 lib64/
cp $SYSROOT/lib64/libc.so.6 lib64/

echo "Making device nodes"
# TODO: Make device nodes
sudo mknod -m 666 dev/null c 1 3
sudo mknod -m 600 dev/console c 5 1

echo "Cleaning and building the writer utility"
# TODO: Clean and build the writer utility
cd $FINDER_APP_DIR
make clean
make CROSS-COMPILE=$CROSS_COMPILE

echo "Copying the finder related scripts to rootfs /home"
# TODO: Copy the finder related scripts and executables to the /home directory
# on the target rootfs
cp finder-test.sh $OUTDIR/rootfs/home/
cp finder.sh $OUTDIR/rootfs/home/
cp writer $OUTDIR/rootfs/home/
cp -r conf/ $OUTDIR/rootfs/home/
cp -r conf/ $OUTDIR/rootfs/
cp autorun-qemu.sh $OUTDIR/rootfs/home/

echo "Chown the root directory"
# TODO: Chown the root directory
cd $OUTDIR/rootfs
sudo chown -R root:root *

echo "Creating initramfs.cpio.gz"
# TODO: Create initramfs.cpio.gz
find . | cpio -H newc -ov --owner root:root > $OUTDIR/initramfs.cpio
cd $OUTDIR
gzip -f initramfs.cpio
cp linux-stable/arch/arm64/boot/Image .

