.PHONY: all libc kernel modules iso clean
all: iso

kernel:
	make -C kernel

libc:
	make -C libc

modules:
	make -C modules

run:
	qemu-system-x86_64 -M q35 -m 256M -cdrom strawos-x86_64.iso -netdev user,id=n0,hostfwd=tcp::8080-:80 -device rtl8139,netdev=n0

iso: libc kernel modules
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

limine:
	git clone https://codeberg.org/Limine/Limine.git limine --branch=v10.x-binary --depth=1
	make -C limine

.PHONY: clean
clean:
	rm -rf limine iso_root