g++ -m32 -march=i686 -c test.cpp -o source.o -nostdlib -I../libs/libc/include -I../libs/openlibm/include -I../libs/openlibm/src
nasm -f elf32 test.asm -o test.o
ld -m elf_i386 -T linker.ld -o test.bin -nostdlib --oformat binary source.o test.o ../libs/libc/libc.a ../libs/openlibm/libopenlibm.a --ignore-unresolved-symbol _GLOBAL_OFFSET_TABLE_
#ld -m elf_i386 -T linker.ld -o test.elf --oformat elf32-i386 source.o test.o

#nasm -f bin -o test.bin test.asm