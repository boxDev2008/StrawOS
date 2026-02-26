override OUTPUT := kernel

CC        := gcc
AS        := nasm
LD        := ld

CFLAGS    := \
    -ffreestanding \
    -fno-stack-protector \
    -fno-pic -fno-pie \
    -mno-red-zone \
    -mno-sse -mno-sse2 -mno-mmx \
    -mcmodel=kernel \
    -O2 -Wall -Wextra -std=c11 \
	-Isrc \
    -Isrc/libk

NASMFLAGS := -f elf64 \
             -g -F dwarf \
             -w+ 

LDFLAGS   := \
    -melf_x86_64 \
    -nostdlib \
    -static \
    -z max-page-size=0x1000 \
    -T linker.ld

override SRCFILES := $(shell find -L src -type f 2>/dev/null | LC_ALL=C sort)
override CFILES := $(filter %.c,$(SRCFILES))
override ASFILES := $(filter %.S,$(SRCFILES))
override NASMFILES := $(filter %.asm,$(SRCFILES))
override OBJ := $(addprefix obj/,$(CFILES:.c=.c.o) $(ASFILES:.S=.S.o) $(NASMFILES:.asm=.asm.o))

.PHONY: all
all: bin/$(OUTPUT)

bin/$(OUTPUT): $(OBJ)
	rm -rf bin
	mkdir -p "$(dir $@)"
	$(LD) $(LDFLAGS) $(OBJ) -o $@

obj/%.c.o: %.c
	mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

obj/%.asm.o: %.asm
	mkdir -p "$(dir $@)"
	nasm $(NASMFLAGS) $< -o $@

.PHONY: clean
clean:
	rm -rf bin obj