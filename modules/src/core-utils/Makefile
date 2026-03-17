CC      = gcc
LD      = ld

LIB  := ../../lib

CFLAGS  := \
    -w \
    -ffreestanding \
    -fno-stack-protector \
    -O2 -Wall \
    -std=c11 \
    -I$(LIB)/libc/include \
    -I$(LIB)/lua/src

LDFLAGS := \
    -melf_x86_64 \
    -nostdlib \
    -T ../module.ld

LIBC := $(LIB)/libc/libc.a

CRT     = ../../lib/crt0/crt0.o

SRCDIR  = src
BINDIR  = ../../bin

SRCS    = $(wildcard $(SRCDIR)/*.c)
ELFS    = $(patsubst $(SRCDIR)/%.c, $(BINDIR)/%.elf, $(SRCS))

.PHONY: all clean

all: $(ELFS)

$(BINDIR)/%.elf: $(SRCDIR)/%.c $(CRT)
	@mkdir -p $(BINDIR) obj
	$(CC) $(CFLAGS) -c $< -o obj/$*.o
	$(LD) $(LDFLAGS) $(CRT) obj/$*.o $(LIBC) -o $@

clean:
	rm -f $(ELFS) obj