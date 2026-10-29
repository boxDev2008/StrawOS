#!/bin/bash
set -e
cd "$(dirname "$0")"

# lite-xl and its libraries (FreeType, PCRE2), built into static archives that build.sh links
# with libc, Lua and StrawX. SDL3 is replaced by the StrawX shim in lib/lite-xl-port.

XL=lib/lite-xl
PORT=lib/lite-xl-port
GEN=obj/lite-xl-gen
OBJ=obj/lite-xl

# pcre2 ships its configured headers and character tables as templates
if [ ! -f $GEN/pcre2/config.h ] || [ lib/pcre2/src/pcre2.h.generic -nt $GEN/pcre2/pcre2.h ]; then
    mkdir -p $GEN/pcre2
    cp lib/pcre2/src/config.h.generic $GEN/pcre2/config.h
    cp lib/pcre2/src/pcre2.h.generic $GEN/pcre2/pcre2.h
    cp lib/pcre2/src/pcre2_chartables.c.dist $GEN/pcre2/pcre2_chartables.c
fi

# runtime data, with the patches in lib/lite-xl-port applied (lua55: Lua 5.5 compatibility;
# root-dir: running from /bin with projects at /; no-home: no $HOME on StrawOS) and the
# version meson would have filled in
DATA=obj/lite-xl-data
if [ ! -f $DATA/core/start.lua ] || [ -n "$(find $PORT -name '*.patch' -newer $DATA/core/start.lua -print -quit)" ] ||
   [ lite-xl.sh -nt $DATA/core/start.lua ] || [ -n "$(find $XL/data -newer $DATA/core/start.lua -print -quit)" ]; then
    rm -rf $DATA
    cp -r $XL/data $DATA
    for p in $PORT/*.patch; do patch -s --no-backup-if-mismatch -d $DATA -p1 < $p; done
    sed -i 's/@PROJECT_VERSION@/2.1.8/' $DATA/core/start.lua
fi

BASE="--target=x86_64-unknown-none -O2 -ffreestanding -mno-red-zone -fno-math-errno -fpie -w
      -Ilib/libc/include"

FT_FLAGS="-DFT2_BUILD_LIBRARY -DFT_CONFIG_MODULES_H='<strawos-ftmodule.h>'
          -I$PORT/freetype -Ilib/freetype/include"
PCRE2_FLAGS="-DHAVE_CONFIG_H -DPCRE2_CODE_UNIT_WIDTH=8 -DPCRE2_STATIC -DSUPPORT_PCRE2_8 -DSUPPORT_UNICODE
             -I$GEN/pcre2 -Ilib/pcre2/src"
XL_FLAGS="-include $PORT/lua55_compat.h -I$PORT/include -I$XL/src -Ilib/lua -Ilib/freetype/include -I$GEN/pcre2 -DPCRE2_STATIC
          -DLITE_ARCH_TUPLE='\"x86_64-strawos\"' -DLITE_PROJECT_VERSION_STR='\"2.1.8\"'"

JOBS=()

# queue $3... for compiling into archive $1 with flags $2, if the source (or this script) changed
add() {
    local lib=$1 flags=$2
    shift 2
    mkdir -p $OBJ/$lib
    for src in "$@"; do
        local obj=$OBJ/$lib/$(echo "$src" | tr / _ | sed 's/\.c$/.o/')
        eval "OBJS_$lib+=(\"\$obj\")"
        if [ ! -f "$obj" ] || [ "$src" -nt "$obj" ] || [ lite-xl.sh -nt "$obj" ] ||
           [ -n "$(find $PORT -name '*.h' -newer "$obj" -print -quit)" ]; then
            JOBS+=("$src"$'\t'"$obj"$'\t'"$(echo $flags)")
        fi
    done
}

in_dir() {
    local dir=$1
    shift
    for f in "$@"; do echo "$dir/$f"; done
}

F=lib/freetype/src
add freetype "$FT_FLAGS" \
    $(in_dir $F/base ftsystem.c ftinit.c ftdebug.c ftbase.c ftbitmap.c ftglyph.c ftmm.c) \
    $F/autofit/autofit.c $F/truetype/truetype.c $F/cff/cff.c $F/sfnt/sfnt.c \
    $F/psaux/psaux.c $F/psnames/psnames.c $F/pshinter/pshinter.c \
    $F/smooth/smooth.c $F/raster/raster.c $F/gzip/ftgzip.c

P=lib/pcre2/src
add pcre2 "$PCRE2_FLAGS" \
    $(in_dir $P pcre2_auto_possess.c pcre2_chkdint.c pcre2_compile.c pcre2_compile_cgroup.c \
        pcre2_compile_class.c pcre2_config.c pcre2_context.c pcre2_convert.c pcre2_dfa_match.c \
        pcre2_error.c pcre2_extuni.c pcre2_find_bracket.c pcre2_jit_compile.c pcre2_maketables.c \
        pcre2_match.c pcre2_match_data.c pcre2_match_next.c pcre2_newline.c pcre2_ord2utf.c \
        pcre2_pattern_info.c pcre2_script_run.c pcre2_serialize.c pcre2_string_utils.c \
        pcre2_study.c pcre2_substitute.c pcre2_substring.c pcre2_tables.c pcre2_ucd.c \
        pcre2_valid_utf.c pcre2_xclass.c) \
    $GEN/pcre2/pcre2_chartables.c

# upstream sources, minus api/process.c (replaced) and the platform dirmonitor backends
add litexl "$XL_FLAGS" \
    $(in_dir $XL/src main.c renderer.c renwindow.c rencache.c arena_allocator.c) \
    $(in_dir $XL/src/api api.c renderer.c regex.c system.c utf8.c dirmonitor.c dirmonitor/dummy.c) \
    $PORT/process.c
add litexl "$XL_FLAGS -Ilib/strawx -Ilib/stb" $PORT/sdl.c $PORT/main.c

if [ ${#JOBS[@]} -gt 0 ]; then
    printf '%s\n' "${JOBS[@]}" | xargs -d '\n' -P"$(nproc)" -I{} sh -c '
        IFS="	" read -r src obj flags <<EOF
{}
EOF
        eval clang $0 $flags -c "$src" -o "$obj" 2>&1 | grep -E "error" | head -5 | sed "s|^|$src: |"
    ' "$(echo $BASE)"
fi

for lib in freetype pcre2 litexl; do
    eval "objs=(\"\${OBJS_$lib[@]}\")"
    missing=0
    for o in "${objs[@]}"; do [ -f "$o" ] || missing=$((missing + 1)); done
    if [ $missing -gt 0 ]; then
        echo "lite-xl.sh: $missing $lib object(s) failed to compile" >&2
        exit 1
    fi
    if [ ! -f obj/lib$lib.a ] || [ -n "$(find "${objs[@]}" -newer obj/lib$lib.a -print -quit)" ]; then
        rm -f obj/lib$lib.a
        llvm-ar rcs obj/lib$lib.a "${objs[@]}"
    fi
done
