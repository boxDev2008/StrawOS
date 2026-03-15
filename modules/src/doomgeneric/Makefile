LD      := ld

BIN  := ../../bin
LIB  := ../../lib
LIBC := $(LIB)/libc/libc.a

CFLAGS  := \
    -w \
    -ffreestanding \
	-nostdlib \
    -fno-stack-protector \
    -Wall \
    -std=c11 \
    -I$(LIB)/libc/include

LDFLAGS := \
    -melf_x86_64 \
    -nostdlib \
    -T ../module.ld

.PHONY: all
all: $(BIN)/doomgeneric.elf

override SRCFILES := $(shell find -L src -type f 2>/dev/null | LC_ALL=C sort)
override CFILES := $(filter %.c,$(SRCFILES))
override OBJ := $(addprefix obj/,$(CFILES:.c=.c.o))

$(BIN)/doomgeneric.elf: $(OBJ)
	$(LD) $(LDFLAGS) $(LIB)/crt0/crt0.o $^ $(LIBC) -o $@

obj/%.c.o: %.c
	mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

clean:
	rm -rf obj