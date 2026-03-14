CC      := gcc
AS      := nasm
LD      := ld

BIN  := ../../bin
LIB  := ../../lib
LIBC := $(LIB)/libc/libc.a
LIBLUA := $(LIB)/lua/liblua.a

CFLAGS  := \
    -w \
    -ffreestanding \
    -fno-stack-protector \
    -O3 -Wall \
    -std=c11 \
    -I$(LIB)/libc/include \
    -I$(LIB)/lua/src

LDFLAGS := \
    -melf_x86_64 \
    -nostdlib \
    -T ../module.ld

.PHONY: all clean

all: $(BIN)/shell.elf

shell.o: shell.c
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN)/shell.elf: $(LIB)/crt0/crt0.o shell.o ../module.ld
	$(LD) $(LDFLAGS) $(LIB)/crt0/crt0.o shell.o $(LIBLUA) $(LIBC) -o $@

clean:
	rm -f *.o
