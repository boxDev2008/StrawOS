CC = g++
CFLAGS = -c -m32 -march=i686 -std=gnu99 -nostdlib -fpermissive -fno-stack-protector -Iinclude

override CFILES := $(shell find -L * -type f -name '*.c')
override OBJ := $(addprefix obj/,$(CFILES:.c=.c.o))
override HEADER_DEPS := $(addprefix obj/,$(CFILES:.c=.c.d))

.PHONY: all
all: libc.a

libc.a: Makefile $(OBJ)
	mkdir -p "$$(dirname $@)"
	ar rcs $@ $(OBJ)

-include $(HEADER_DEPS)

obj/%.c.o: %.c Makefile
	mkdir -p "$$(dirname $@)"
	$(CC) $(CFLAGS) -c $< -o $@

obj/arith64.c.o: arith64.c Makefile
	mkdir -p "$$(dirname $@)"
	gcc -c -m32 -march=i686 -fno-stack-protector -c $< -o $@

.PHONY: clean
clean:
	rm -rf obj
	rm libc.a