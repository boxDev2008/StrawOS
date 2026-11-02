#!/bin/bash
set -e
cd "$(dirname "$0")"

SRC=lib/opus
OBJ=obj/opus

list() {
    sed -n "/^$1 =/,/^$/p" "$SRC/$2" | grep -o '[a-z_/]*[A-Za-z0-9_]*\.c'
}

SOURCES=$(list OPUS_SOURCES opus_sources.mk; list CELT_SOURCES celt_sources.mk;
          list SILK_SOURCES silk_sources.mk; list SILK_SOURCES_FIXED silk_sources.mk)

CFLAGS="--target=x86_64-unknown-none -O2 -ffreestanding -mno-red-zone -fno-math-errno -fpie -w
        -Ilib/libc/include -I$SRC/include -I$SRC/celt -I$SRC/silk -I$SRC/silk/fixed
        -DOPUS_BUILD -DFIXED_POINT -DDISABLE_FLOAT_API -DVAR_ARRAYS -DHAVE_STDINT_H"

mkdir -p $OBJ
STALE=$(for src in $SOURCES; do
    obj=$OBJ/$(echo "${src%.c}" | tr / _).o
    if [ ! -f "$obj" ] || [ "$SRC/$src" -nt "$obj" ] || [ opus.sh -nt "$obj" ]; then echo "$SRC/$src $obj"; fi
done)

if [ -n "$STALE" ] || [ ! -f obj/libopus.a ]; then
    if [ -n "$STALE" ]; then
        echo "$STALE" | xargs -P"$(nproc)" -n2 sh -c "clang $(echo $CFLAGS) -c \"\$0\" -o \"\$1\""
    fi
    rm -f obj/libopus.a
    llvm-ar rcs obj/libopus.a $(for src in $SOURCES; do echo $OBJ/$(echo "${src%.c}" | tr / _).o; done)
fi
