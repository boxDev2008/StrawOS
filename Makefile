.PHONY: all
all:
	make -C libs/libk all
	make -C libs/libc all
	make -C libs/openlibm libopenlibm.a ARCH=i386
	make -C kernel all
	make iso

.PHONY: clean
clean:
	make -C kernel clean
	make -C libs/libk clean
	make -C libs/libc clean
	make -C libs/openlibm clean
	rm isodir/boot/kernel.bin

.PHONY: limine
limine:
	git clone https://github.com/limine-bootloader/limine.git --branch=v7.x-binary --depth=1
	make -C limine

.PHONY: iso
iso:
	mkdir -p iso_root
	mkdir -p iso_root/boot
	cp -v kernel/bin/kernel iso_root/boot/
	mkdir -p iso_root/boot/limine
	cp -v limine.cfg limine/limine-bios.sys limine/limine-bios-cd.bin \
		limine/limine-uefi-cd.bin iso_root/boot/limine/
	mkdir -p iso_root/EFI/BOOT
	cp -v limine/BOOTIA32.EFI iso_root/EFI/BOOT/
	xorriso -as mkisofs -b boot/limine/limine-bios-cd.bin \
			-no-emul-boot -boot-load-size 4 -boot-info-table \
			--efi-boot boot/limine/limine-uefi-cd.bin \
			-efi-boot-part --efi-boot-image --protective-msdos-label \
			iso_root -o strawos.iso
	./limine/limine bios-install strawos.iso

.PHONY: run
run:
	qemu-system-i386 -display sdl -m 512M -cdrom strawos.iso -drive file=hd.img,if=ide,format=raw -boot d