.PHONY: all clean

all:
	make -C lib/libc
	make -C lib/lua
	make -C lib/crt0
	make -C src/shell

clean:
	make -C lib/libc clean
	make -C lib/lua clean
	make -C lib/crt0 clean
	make -C src/shell clean
	rm -rf bin/*.elf