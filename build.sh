#!/bin/bash
set -e

rm -rf build
mkdir -p build/iso/boot

./modules/build.sh

clang \
    -target x86_64-unknown-none-elf \
    -O2 \
    -ffreestanding \
    -fno-stack-protector \
    -fno-pic \
    -fno-pie \
    -mno-red-zone \
    -mno-sse \
    -mno-sse2 \
    -mcmodel=kernel \
    -c kernel/build.c \
    -o build/kernel.o

ld.lld \
    -m elf_x86_64 \
    -nostdlib \
    -static \
    -T kernel/linker.ld \
    build/kernel.o \
    -o build/iso/boot/kernel.elf

cp limine/limine-bios.sys build/iso/boot/
cp limine/limine-bios-cd.bin build/iso/boot/
cp limine/limine-uefi-cd.bin build/iso/boot/

mkdir -p build/iso/EFI/BOOT

cp limine/BOOTX64.EFI build/iso/EFI/BOOT/
cp limine.conf build/iso/boot/limine.conf

mkdir -p build/root/bin
cp -r modules/root/. build/root/
cp modules/bin/*.elf build/root/bin/
cp -r modules/obj/tcc-sysroot/. build/root/
mkdir -p build/root/share/netsurf build/root/share/fonts
cp -r modules/obj/nsgen/res/. build/root/share/netsurf/
cp -r modules/obj/lite-xl-data build/root/share/lite-xl
for f in NotoSans-Regular NotoSans-Bold NotoSans-Italic NotoSans-BoldItalic NotoSans-Light \
         NotoSerif-Regular NotoSerif-Bold NotoSerif-Italic NotoSerif-BoldItalic \
         NotoSansMono-Regular NotoSansMono-Bold; do
    cp /usr/share/fonts/noto/$f.ttf build/root/share/fonts/
done
tar --format=ustar -C build/root -cf build/iso/boot/root.tar .

rm -f strawos-x86_64.iso

xorriso -as mkisofs \
    -R -r -J \
    -b boot/limine-bios-cd.bin \
    -no-emul-boot \
    -boot-load-size 4 \
    -boot-info-table \
    -eltorito-alt-boot \
    -e boot/limine-uefi-cd.bin \
    -no-emul-boot \
    -append_partition 2 0xef limine/limine-uefi-cd.bin \
    build/iso \
    -o strawos-x86_64.iso

# limine bios-install strawos-x86_64.iso