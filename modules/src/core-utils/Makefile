CC      = gcc
LD      = ld
LIB     := ../../lib
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
LIBC    := $(LIB)/libc/libc.a
CRT     := ../../lib/crt0/crt0.o
BINDIR  := ../../bin

override SRCFILES := $(shell find -L src -type f 2>/dev/null | LC_ALL=C sort)
override CFILES   := $(filter %.c,$(SRCFILES))
override OBJS     := $(addprefix obj/,$(CFILES:.c=.c.o))
override ELFS     := $(patsubst src/%.c,$(BINDIR)/%.elf,$(CFILES))

.PHONY: all clean

all: $(ELFS)

obj/%.c.o: src/%.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BINDIR)/%.elf: obj/src/%.c.o $(CRT)
	mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) $(CRT) $< $(LIBC) -o $@

clean:
	rm -f $(ELFS) $(OBJS)