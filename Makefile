.PHONY: all limine libc kernel modules iso clean
all: iso

kernel:
	make -C kernel

libc:
	make -C libc

modules:
	make -C modules

limine:
	make -C limine

run:
	qemu-system-x86_64 -display sdl -m 512M -cdrom strawos-x86_64.iso

iso: limine libc kernel modules
	mkdir -p iso_root/boot/limine iso_root/EFI/BOOT
	cp kernel/bin/kernel iso_root/boot/
	cp -r modules/ iso_root/
	cp limine.conf iso_root/boot/limine/
	cp limine/limine-bios.sys limine/limine-bios-cd.bin \
	   limine/limine-uefi-cd.bin iso_root/boot/limine/
	cp limine/BOOTX64.EFI iso_root/EFI/BOOT/ 2>/dev/null || true
	xorriso -as mkisofs -b boot/limine/limine-bios-cd.bin  \
	    -no-emul-boot -boot-load-size 4 -boot-info-table   \
	    --efi-boot boot/limine/limine-uefi-cd.bin          \
	    -efi-boot-part --efi-boot-image                    \
	    --protective-msdos-label iso_root -o strawos-x86_64.iso
	limine/limine bios-install strawos-x86_64.iso

.PHONY: clean
clean:
	make -C kernel clean
	make -C libc clean
	make -C modules clean
	rm -rf iso_root