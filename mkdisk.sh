#!/bin/bash
cd rootfs
./compile.sh
cd ..
printf "Creating Disk...\n"
dd if=/dev/zero of=hd.img bs=1024 count=8192 > /dev/zero
mkfs.fat hd.img 
printf "Mounting...\n"
rm -rf /mnt/fat32
mkdir /mnt/fat32
mount hd.img /mnt/fat32
printf "Mounted!\n"
cp -r rootfs/* /mnt/fat32
umount hd.img
printf "Done!\n"