#!/bin/bash
set -e
cd "$(dirname "$0")"

TCC=lib/tinycc
OBJ=obj/tcc
SYSROOT=obj/tcc-sysroot

CFLAGS="--target=x86_64-unknown-none -O2 -ffreestanding -nostdlibinc -mno-red-zone -fno-math-errno -fpie -w
        -Ilib/libc/include"

STAMP=$OBJ/sysroot.stamp
if [ -f $STAMP ] && [ -z "$(find tcc.sh crt0.c lib/libc lib/strawx lib/stb $TCC/include $TCC/lib -newer $STAMP -print -quit)" ]; then
    exit 0
fi

rm -rf $SYSROOT
mkdir -p $OBJ $SYSROOT/usr/include/strawx $SYSROOT/usr/lib/tcc/include

# headers: libc for /usr/include, tcc's own (stdarg.h, tccdefs.h, ...) for /usr/lib/tcc/include,
# skipping the ones libc already provides so the types stay consistent with libc.a
cp -r lib/libc/include/. $SYSROOT/usr/include/
for h in $TCC/include/*.h; do
    [ -f lib/libc/include/$(basename $h) ] || cp $h $SYSROOT/usr/lib/tcc/include/
done

# startup files: tcc links crt1.o crti.o ... crtn.o around every executable
clang $CFLAGS -c crt0.c -o $SYSROOT/usr/lib/crt1.o
echo "" | clang $CFLAGS -x c -c - -o $SYSROOT/usr/lib/crti.o
echo "" | clang $CFLAGS -x c -c - -o $SYSROOT/usr/lib/crtn.o

clang $CFLAGS -c lib/libc/libc.c -o $OBJ/libc.o
llvm-ar rcs $SYSROOT/usr/lib/libc.a $OBJ/libc.o

# strawx: headers get their own dir (generic names like settings.h/theme.h) which tcc searches
# by default, so programs use <strawx.h> like the built-in apps; link with -lstrawx
cp lib/strawx/*.h $SYSROOT/usr/include/strawx/
clang $CFLAGS -Ilib/strawx -Ilib/stb -c lib/strawx/strawx.c -o $OBJ/strawx.o
llvm-ar rcs $SYSROOT/usr/lib/libstrawx.a $OBJ/strawx.o

# tcc runtime support (va_arg helpers, atomics, alloca, ...)
L1="libtcc1.c stdatomic.c atomic.S builtin.c alloca.S alloca-bt.S va_list.c dsohandle.c"
L1_OBJS=()
for src in $L1; do
    obj=$OBJ/${src%.*}.o
    clang $CFLAGS -fno-omit-frame-pointer -I$TCC -DTCC_TARGET_X86_64 -c $TCC/lib/$src -o $obj
    L1_OBJS+=($obj)
done
llvm-ar rcs $SYSROOT/usr/lib/tcc/libtcc1.a "${L1_OBJS[@]}"
clang $CFLAGS -I$TCC -DTCC_TARGET_X86_64 -c $TCC/lib/runmain.c -o $SYSROOT/usr/lib/tcc/runmain.o

touch $STAMP
