#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.

set -e
set -u

OUTDIR=/tmp/aeld
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.15.163
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath $(dirname $0))
ARCH=arm64
CROSS_COMPILE=aarch64-none-linux-gnu-

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

	make ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- mrproper
	make ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- defconfig
	make -j4 ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- all
#	make ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- modules
	make ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- dtbs
fi

echo "Adding the Image in outdir"
cp ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ${OUTDIR}/
cp ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image.gz ${OUTDIR}/

echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]
then
	echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm  -rf ${OUTDIR}/rootfs
fi

# TODO: Create necessary base directories
mkdir ${OUTDIR}/rootfs
cd ${OUTDIR}/rootfs
mkdir bin dev etc home lib lib64 proc sbin sys tmp usr var
mkdir usr/bin usr/lib usr/sbin
mkdir var/log

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/busybox" ]
then
git clone git://busybox.net/busybox.git
    cd busybox
git config --global pack.windowMemory "100m"
git config --global pack.SizeLimit "100m" 
git config --global pack.threads "1"
git config --global pack.window "0"
    git checkout ${BUSYBOX_VERSION}
    # TODO:  Configure busybox
	make distclean
    	make defconfig

else
	cd busybox
fi
echo "running busybox make"
    #make distclean
   # make defconfig    
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE}
    make CONFIG_PREFIX=${OUTDIR}/rootfs ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} install
# TODO: Make and install busybox

cd ${OUTDIR}/rootfs
echo "Library dependencies"
${CROSS_COMPILE}readelf -a bin/busybox | grep "program interpreter"
${CROSS_COMPILE}readelf -a bin/busybox | grep "Shared library"

# TODO: Add library dependencies to rootfs
cp /home/arjavgarg/Data/arm-cross-compiler/arm-gnu-toolchain-14.2.rel1-x86_64-aarch64-none-linux-gnu/bin/../aarch64-none-linux-gnu/libc/lib/ld-linux-aarch64.so.1 lib
cp /home/arjavgarg/Data/arm-cross-compiler/arm-gnu-toolchain-14.2.rel1-x86_64-aarch64-none-linux-gnu/bin/../aarch64-none-linux-gnu/libc/lib64/libm.so.6 lib64
cp /home/arjavgarg/Data/arm-cross-compiler/arm-gnu-toolchain-14.2.rel1-x86_64-aarch64-none-linux-gnu/bin/../aarch64-none-linux-gnu/libc/lib64/libresolv.so.2 lib64
cp /home/arjavgarg/Data/arm-cross-compiler/arm-gnu-toolchain-14.2.rel1-x86_64-aarch64-none-linux-gnu/bin/../aarch64-none-linux-gnu/libc/lib64/libc.so.6 lib64

# TODO: Make device nodes
sudo mknod -m 666 dev/null c 1 3
sudo mknod -m 600 dev/console c 5 1

# TODO: Clean and build the writer utility
cd ${FINDER_APP_DIR}/finder-app
make clean
make

# TODO: Copy the finder related scripts and executables to the /home directory
# on the target rootfs
cd ${FINDER_APP_DIR}/finder-app
make clean
make CROSS_COMPILE=${CROSS_COMPILE}
cp finder.sh writer ${OUTDIR}/rootfs/home
cd ..
cp finder-test.sh ${OUTDIR}/rootfs/home
cd ..
cp -r conf ${OUTDIR}/rootfs/home
cd ${FINDER_APP_DIR}
cp autorun-qemu.sh ${OUTDIR}/rootfs/home

# TODO: Chown the root directory
cd ${OUTDIR}/rootfs
sudo chown -R root:root *

# TODO: Create initramfs.cpio.gz
cd ${OUTDIR}/rootfs
#rm ${OUTDIR}/initramfs.cpio
find . | cpio -H newc -ov --owner root:root > ${OUTDIR}/initramfs.cpio
cd ${OUTDIR}
#ls
#rm initramfs.cpio.gz
#ls
gzip -f initramfs.cpio
