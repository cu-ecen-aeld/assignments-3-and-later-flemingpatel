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

    # kernel build steps here

    # clean up - use with caution
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} mrproper

    # build steps
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} defconfig
    make -j4 ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} all
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} dtbs

    # Install modules into the root filesystem
    # to install we need rootfs first
    # make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} modules_install INSTALL_MOD_PATH=${OUTDIR}/rootfs
fi

echo "Adding the Image in outdir"
cp -rp ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ${OUTDIR}/.

echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]
then
	echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
  sudo rm  -rf ${OUTDIR}/rootfs
fi

# Create necessary base directories
mkdir -p ${OUTDIR}/rootfs/{bin,dev,etc,home,lib,lib64,proc,sbin,sys,tmp,usr,var}
mkdir -p ${OUTDIR}/rootfs/usr/{bin,lib,sbin}
mkdir -p ${OUTDIR}/rootfs/var/log

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/busybox" ]
then
git clone git://busybox.net/busybox.git
    cd busybox
    git checkout ${BUSYBOX_VERSION}
    # Configure busybox
    make distclean
    make defconfig
else
    cd busybox
fi

# Make and install busybox
make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} CONFIG_PREFIX=${OUTDIR}/rootfs install

if [ ! -e ${OUTDIR}/rootfs/bin/busybox ]; then
  echo "BusyBox binary not found"
  exit 1
fi

# busybox binary
# setuid root to ensure all configured applets will
# work properly
sudo chmod 4755 ${OUTDIR}/rootfs/bin/busybox

echo "Library dependencies"
${CROSS_COMPILE}readelf -a ${OUTDIR}/rootfs/bin/busybox | grep "program interpreter"
${CROSS_COMPILE}readelf -a ${OUTDIR}/rootfs/bin/busybox | grep "Shared library"

# Add library dependencies to rootfs
SYSROOT=$(${CROSS_COMPILE}gcc -print-sysroot)
LINKER=$(${CROSS_COMPILE}readelf -a ${OUTDIR}/rootfs/bin/busybox | grep "program interpreter" | awk -F ': ' '{print $2}' | tr -d ']')
SHARED_LIBS=$(${CROSS_COMPILE}readelf -a ${OUTDIR}/rootfs/bin/busybox | grep "Shared library" | awk '{print $5}' | tr -d '[]' | tr '\n' ' ')

# Add dynamic linker/loader to rootfs
echo "Copying linker ${LINKER} to ${OUTDIR}/rootfs/${LINKER}"
cp -p "${SYSROOT}${LINKER}" "${OUTDIR}/rootfs/${LINKER}"

# Add shared libraries to rootfs dynamically
for lib in ${SHARED_LIBS}; do
  # Find the library in the sysroot
  LIB_PATH=$(find ${SYSROOT} -name ${lib})
  if [ -n "${LIB_PATH}" ]; then
    echo "Copying shared lib ${lib} to ${OUTDIR}/rootfs/$(dirname ${LIB_PATH#${SYSROOT}})"
    cp -p ${LIB_PATH} ${OUTDIR}/rootfs/$(dirname ${LIB_PATH#${SYSROOT}})
  else
    echo "Library ${lib} not found in sysroot"
    exit 1
  fi
done

# Make device nodes
sudo mknod -m 666 ${OUTDIR}/rootfs/dev/null c 1 3
sudo mknod -m 600 ${OUTDIR}/rootfs/dev/console c 5 1

# Clean and build the writer utility
cd ${FINDER_APP_DIR}
make clean
make CROSS_COMPILE=${CROSS_COMPILE}

# Copy the finder related scripts and executables to the /home directory
# on the target rootfs
sudo cp -p ${FINDER_APP_DIR}/writer ${OUTDIR}/rootfs/home/
sudo cp -p ${FINDER_APP_DIR}/finder.sh ${OUTDIR}/rootfs/home/
sudo cp -p ${FINDER_APP_DIR}/finder-test.sh ${OUTDIR}/rootfs/home/
sudo cp -p ${FINDER_APP_DIR}/conf/username.txt ${OUTDIR}/rootfs/home/
sudo cp -p ${FINDER_APP_DIR}/conf/assignment.txt ${OUTDIR}/rootfs/home/
sudo cp -p ${FINDER_APP_DIR}/autorun-qemu.sh ${OUTDIR}/rootfs/home/

# Modify the finder-test.sh and finder.sh script - hate to do this
sed -i 's|\.\./conf/assignment.txt|/home/assignment.txt|g' ${OUTDIR}/rootfs/home/finder-test.sh
sed -i 's|conf/username.txt|/home/username.txt|g' ${OUTDIR}/rootfs/home/finder-test.sh
sed -i 's|bash|sh|g' ${OUTDIR}/rootfs/home/finder.sh

# Chown the root directory
sudo chown -R root:root ${OUTDIR}/rootfs

# Create initramfs.cpio.gz
cd ${OUTDIR}/rootfs
find . | cpio -H newc -ov --owner root:root > ${OUTDIR}/initramfs.cpio
gzip -f ${OUTDIR}/initramfs.cpio
