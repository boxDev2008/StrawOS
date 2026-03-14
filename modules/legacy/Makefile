CC      := gcc
AS      := nasm
LD      := ld

LIBC := ../libc/libc.a

CFLAGS  := \
    -w \
    -ffreestanding \
    -fno-stack-protector \
    -O3 -Wall \
    -std=c11 \
    -I../libc/include

ASFLAGS := -f elf64

LDFLAGS := \
    -melf_x86_64 \
    -nostdlib \
    -T module.ld

.PHONY: all clean

all: test.elf

crt0.o: crt0.asm
	$(AS) $(ASFLAGS) $< -o $@

test.o: test.c
	$(CC) $(CFLAGS) -c $< -o $@

bmp.o: bmp.c
	$(CC) $(CFLAGS) -c $< -o $@

test.elf: crt0.o test.o bmp.o module.ld
	$(LD) $(LDFLAGS) crt0.o test.o bmp.o $(LIBC) -o $@

clean:
	rm -f *.o *.elf
