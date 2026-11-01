#!/bin/bash
set -e
cd "$(dirname "$0")"

# ClassiCube (with its bundled BearSSL and FreeType), built into a static archive that
# build.sh links with libc and StrawX. The StrawOS platform and StrawX window backends live
# in lib/classicube-port; config.h picks the build options.

CC_SRC=lib/classicube/src
PORT=lib/classicube-port
GEN=obj/classicube-gen
OBJ=obj/classicube

# StrawOS has no threads, so queued HTTP requests run on the spot, as on the PSP and NDS
mkdir -p $GEN
if [ ! -f $GEN/Http_Worker.c ] || [ $CC_SRC/Http_Worker.c -nt $GEN/Http_Worker.c ] || [ classicube.sh -nt $GEN/Http_Worker.c ]; then
    sed 's/#if defined CC_BUILD_PSP || defined CC_BUILD_NDS\r\?$/#if defined CC_BUILD_PSP || defined CC_BUILD_NDS || defined CC_BUILD_STRAWOS/' \
        $CC_SRC/Http_Worker.c > $GEN/Http_Worker.c
    grep -q 'CC_BUILD_STRAWOS' $GEN/Http_Worker.c || { echo "classicube.sh: Http_Worker.c patch no longer applies" >&2; exit 1; }
fi

# The base texture pack is ClassiCube's own (fetched below and put in the image by build.sh)
# rather than the default.zip the launcher builds from Minecraft jars it downloads from Mojang.
# It has every file the game expects of a base pack, so nothing is reported missing and the
# launcher never offers that download.
if [ ! -f $GEN/GameVersion.c ] || [ $CC_SRC/GameVersion.c -nt $GEN/GameVersion.c ] || [ classicube.sh -nt $GEN/GameVersion.c ]; then
    sed 's|"texpacks/default[_0-9]*\.zip"|"texpacks/classicube.zip"|' $CC_SRC/GameVersion.c > $GEN/GameVersion.c
    grep -q '"texpacks/default' $GEN/GameVersion.c && { echo "classicube.sh: GameVersion.c patch no longer applies" >&2; exit 1; }
fi

# fixes to upstream sources, applied to copies in $GEN (see each .patch for what it fixes)
for patchfile in $PORT/*.patch; do
    target=$(sed -n 's|^+++ b/\([^[:space:]]*\).*|\1|p' "$patchfile" | head -1)
    if [ ! -f $GEN/$target ] || [ $CC_SRC/$target -nt $GEN/$target ] || [ "$patchfile" -nt $GEN/$target ] ||
       [ classicube.sh -nt $GEN/$target ]; then
        cp $CC_SRC/$target $GEN/$target
        patch -s --no-backup-if-mismatch -d $GEN -p1 < "$patchfile" ||
            { rm -f $GEN/$target; echo "classicube.sh: $patchfile no longer applies" >&2; exit 1; }
    fi
done
PATCHED=$(sed -n 's|^+++ b/\([^[:space:]]*\).*|\1|p' $PORT/*.patch | tr '\n' ' ')

RES=obj/classicube-res
if [ ! -f $RES/classicube.zip ]; then
    mkdir -p $RES
    if ! curl -sSfL -o $RES/classicube.zip.part http://static.classicube.net/default.zip; then
        rm -f $RES/classicube.zip.part
        echo "classicube.sh: cannot download the ClassiCube texture pack (static.classicube.net/default.zip)" >&2
        exit 1
    fi
    mv $RES/classicube.zip.part $RES/classicube.zip
fi

BASE="--target=x86_64-unknown-none -O2 -ffreestanding -mno-red-zone -fno-math-errno -fpie -w
      -Ilib/libc/include"
CC_FLAGS="-include $PORT/config.h -I$CC_SRC -Ilib/strawx"

JOBS=()
OBJS=()

# queue $2... for compiling with flags $1, if the source, a port header or this script changed
add() {
    local flags=$1
    shift
    mkdir -p $OBJ
    for src in "$@"; do
        local obj=$OBJ/$(echo "$src" | tr / _ | sed 's/\.c$/.o/')
        OBJS+=("$obj")
        if [ ! -f "$obj" ] || [ "$src" -nt "$obj" ] || [ classicube.sh -nt "$obj" ] ||
           [ $PORT/config.h -nt "$obj" ] || [ lib/strawx/strawx.h -nt "$obj" ] || [ lib/strawx/protocol.h -nt "$obj" ]; then
            JOBS+=("$src"$'\t'"$obj"$'\t'"$(echo $flags)")
        fi
    done
}

GENERATED="Http_Worker.c GameVersion.c $PATCHED"
add "$CC_FLAGS" $(for f in $CC_SRC/*.c; do case " $GENERATED " in *" $(basename $f) "*) ;; *) echo $f;; esac; done) \
    $(for f in $GENERATED; do echo $GEN/$f; done) \
    $PORT/Platform_StrawOS.c $PORT/Window_StrawX.c
add "-Ilib/classicube/third_party/bearssl" lib/classicube/third_party/bearssl/*.c

if [ ${#JOBS[@]} -gt 0 ]; then
    printf '%s\n' "${JOBS[@]}" | xargs -d '\n' -P"$(nproc)" -I{} sh -c '
        IFS="	" read -r src obj flags <<EOF
{}
EOF
        rm -f "$obj"
        eval clang $0 $flags -c "$src" -o "$obj" 2>&1 | grep -E "error" | head -5 | sed "s|^|$src: |"
    ' "$(echo $BASE)"
fi

missing=0
for o in "${OBJS[@]}"; do [ -f "$o" ] || missing=$((missing + 1)); done
if [ $missing -gt 0 ]; then
    echo "classicube.sh: $missing object(s) failed to compile" >&2
    exit 1
fi
# rebuilt from scratch when an object changed or the set of objects did (e.g. a source
# swapped for a patched copy), so no stale duplicate is left for the linker to pick up
MEMBERS=$(printf '%s\n' "${OBJS[@]}")
if [ ! -f obj/libclassicube.a ] || [ "$MEMBERS" != "$(cat obj/libclassicube.members 2>/dev/null)" ] ||
   [ -n "$(find "${OBJS[@]}" -newer obj/libclassicube.a -print -quit)" ]; then
    rm -f obj/libclassicube.a
    llvm-ar rcs obj/libclassicube.a "${OBJS[@]}"
    printf '%s\n' "$MEMBERS" > obj/libclassicube.members
fi
