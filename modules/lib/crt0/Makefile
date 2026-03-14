AS := nasm
ASFLAGS := -f elf64

.PHONY: all clean

all:
	$(AS) $(ASFLAGS) -o crt0.o crt0.asm

clean:
	rm -f crt0.o