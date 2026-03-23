.PHONY: all clean

all:
	make -C lib/libc
	make -C lib/lua
	make -C lib/crt0
	make -C src/core-utils
	make -C src/shell
	make -C src/getgoogle
	make -C src/doomgeneric

clean:
	make -C lib/libc clean
	make -C lib/lua clean
	make -C lib/crt0 clean
	make -C src/core-utils clean
	make -C src/shell clean
	make -C src/getgoogle clean
	make -C src/doomgeneric clean
	rm -rf bin/*.elf