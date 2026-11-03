#!/bin/bash
set -e
cd "$(dirname "$0")"

SRC=lib/libvpx
CFG=obj/libvpx-cfg
OBJ=obj/libvpx

if [ ! -f $CFG/srcs.txt ] || [ vpx.sh -nt $CFG/srcs.txt ]; then
    rm -rf $CFG $OBJ
    mkdir -p $CFG
    (cd $CFG && ../../$SRC/configure --target=x86_64-linux-gcc --as=nasm --enable-pic --enable-runtime-cpu-detect \
        --disable-avx --disable-avx2 --disable-avx512 --enable-vp8 --enable-vp9-decoder --disable-vp9-encoder \
        --disable-multithread --disable-vp9-highbitdepth --disable-postproc --disable-vp9-postproc \
        --disable-examples --disable-tools --disable-docs --disable-unit-tests --disable-install-docs \
        --disable-webm-io --disable-libyuv >/dev/null)
    make -C $CFG vpx_config.asm vpx_dsp_rtcd.h vp8_rtcd.h vp9_rtcd.h vpx_scale_rtcd.h vpx_version.h >/dev/null
    make -C $CFG -n libvpx.a | grep -oE '[^ "]*\.(c|asm)$' | sed "s|^$PWD/$SRC/||; s|^\.\./\.\./$SRC/||" | sort -u \
        > $CFG/srcs.txt
fi

CFLAGS="--target=x86_64-unknown-none -O2 -ffreestanding -mno-red-zone -fno-math-errno -fpie -w -DNDEBUG
        -Ilib/libc/include -I$CFG -I$SRC"

simd_flags() {
    case "$1" in
        *_sse4*) echo -msse4.1 ;;
        *_ssse3*) echo -mssse3 ;;
        *_sse3*) echo -msse3 ;;
        *_sse2*) echo -msse2 ;;
        *_mmx*) echo -mmmx ;;
    esac
}

mkdir -p $OBJ
OBJS=()
STALE=()
for src in $(cat $CFG/srcs.txt); do
    path=$SRC/$src
    [ -f "$path" ] || path=$CFG/$src
    [ -f "$path" ] || continue
    obj=$OBJ/$(echo "$src" | tr / _).o
    OBJS+=("$obj")
    if [ ! -f "$obj" ] || [ "$path" -nt "$obj" ] || [ vpx.sh -nt "$obj" ]; then
        STALE+=("$path:$obj")
    fi
done

if [ ${#STALE[@]} -gt 0 ] || [ ! -f obj/libvpx.a ]; then
    for job in "${STALE[@]}"; do
        path=${job%%:*}
        obj=${job#*:}
        case "$path" in
            *.asm) echo "nasm -f elf64 -I$CFG/ -I$SRC/ -o $obj $path" ;;
            *) echo "clang $(echo $CFLAGS) $(simd_flags "$path") -c $path -o $obj" ;;
        esac
    done | xargs -P"$(nproc)" -I{} sh -c "{}"
    rm -f obj/libvpx.a
    llvm-ar rcs obj/libvpx.a "${OBJS[@]}"
fi
