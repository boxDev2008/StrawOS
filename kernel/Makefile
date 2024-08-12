override KERNEL := kernel

override LIBK := ../libs/libk/libk.a
override OPENLIBM := ../libs/openlibm/libopenlibm.a

ASM = nasm
CC = gcc
LD = ld

NASMFLAGS = -felf32
CFLAGS = -c -w -O3 -m32 -march=i686 -std=gnu99 -Wall -Wextra -fno-stack-protector -Isrc -I../libs/libk/include -I../libs/openlibm/include -I../libs/openlibm/src
LDFLAGS = -m elf_i386

CFLAGS_NO_OPTIMIZE = -c -w -m32 -march=i686 -std=gnu99 -Wall -Wextra -fno-stack-protector -Isrc -I../libs/libk/include -I../libs/openlibm/include -I../libs/openlibm/src

override CFILES := $(shell cd src && find -L * -type f -name '*.c')
override ASFILES := $(shell cd src && find -L * -type f -name '*.S')
override NASMFILES := $(shell cd src && find -L * -type f -name '*.asm')
override OBJ := $(addprefix obj/,$(CFILES:.c=.c.o) $(ASFILES:.S=.S.o) $(NASMFILES:.asm=.asm.o))
override HEADER_DEPS := $(addprefix obj/,$(CFILES:.c=.c.d) $(ASFILES:.S:.S.d))

.PHONY: all
all: bin/$(KERNEL)

bin/$(KERNEL): Makefile linker.ld $(OBJ)
	mkdir -p "$$(dirname $@)"
	$(LD) -T linker.ld $(OBJ) $(LDFLAGS) -o $@ $(LIBK) $(OPENLIBM)

-include $(HEADER_DEPS)

obj/%.c.o: src/%.c Makefile
	mkdir -p "$$(dirname $@)"
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

obj/devices/ide.c.o: src/devices/ide.c Makefile
	mkdir -p "$$(dirname $@)"
	$(CC) $(CFLAGS_NO_OPTIMIZE) $(CPPFLAGS) -c $< -o $@

obj/fs/fatfs%.c.o: src/fs/fatfs%.c Makefile
	mkdir -p "$$(dirname $@)"
	$(CC) $(CFLAGS_NO_OPTIMIZE) $(CPPFLAGS) -c $< -o $@

obj/%.S.o: src/%.S Makefile
	mkdir -p "$$(dirname $@)"
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

obj/%.asm.o: src/%.asm Makefile
	mkdir -p "$$(dirname $@)"
	nasm $(NASMFLAGS) $< -o $@

.PHONY: clean
clean:
	rm -rf bin obj