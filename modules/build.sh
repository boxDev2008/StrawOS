#!/bin/bash
set -e
cd "$(dirname "$0")"

CFLAGS=(--target=x86_64-unknown-none -O2 -ffreestanding -mno-red-zone -fno-math-errno -fpie
        -Ilib/libc/include -Ilib/strawx -Ilib/stb)
LDFLAGS=(--target=x86_64-unknown-none -nostdlib -static -fpie -Wl,-pie -Wl,-no-dynamic-linker)
NET=(-Ilib/bearssl/inc -Ilib/net)
OBJ=obj/build

mkdir -p bin $OBJ obj/bearssl

# true if $1 is missing or older than any of the remaining files
stale() {
    local out=$1
    shift
    [ -f "$out" ] || return 0
    for f in "$@"; do
        [ "$f" -nt "$out" ] && return 0
    done
    return 1
}

PIDS=()

# compile one object in the background; header deps come from clang's .d file
compile() {
    local src=$1 out=$OBJ/$(echo "${1%.c}" | tr / _).o
    shift
    OUT=$out
    if [ -f "$out.d" ] && ! stale "$out" build.sh $(sed -e 's/^[^:]*://' -e 's/\\$//' "$out.d"); then
        return
    fi
    clang "${CFLAGS[@]}" "$@" -MMD -MF "$out.d" -c "$src" -o "$out" &
    PIDS+=($!)
}

wait_all() {
    local failed=0
    for pid in "${PIDS[@]}"; do
        wait "$pid" || failed=1
    done
    PIDS=()
    [ $failed -eq 0 ] || exit 1
}

# link $1 from the remaining objects/archives in the background, if any input is newer
link() {
    local out=$1
    shift
    if stale "$out" "$@"; then
        clang "${LDFLAGS[@]}" -o "$out" "$@" &
        PIDS+=($!)
    fi
}

BEARSSL_CFLAGS="--target=x86_64-unknown-none -O2 -ffreestanding -mno-red-zone -fno-math-errno -fpie -Ilib/libc/include -Ilib/bearssl/inc -Ilib/bearssl/src"
bearssl_stale=$(for src in lib/bearssl/src/*.c lib/bearssl/src/*/*.c; do
    obj=obj/bearssl/$(echo "${src#lib/bearssl/src/}" | tr / _).o
    if [ ! -f "$obj" ] || [ "$src" -nt "$obj" ]; then echo "$src $obj"; fi
done)
if [ -n "$bearssl_stale" ] || [ ! -f obj/libbearssl.a ]; then
    if [ -n "$bearssl_stale" ]; then
        echo "$bearssl_stale" | xargs -P"$(nproc)" -n2 sh -c "clang $BEARSSL_CFLAGS -c \"\$0\" -o \"\$1\""
    fi
    rm -f obj/libbearssl.a
    llvm-ar rcs obj/libbearssl.a obj/bearssl/*.o
fi

./tcc.sh &
PIDS+=($!)
./netsurf.sh &
PIDS+=($!)
./lite-xl.sh &
PIDS+=($!)

compile crt0.c;            CRT0=$OUT
compile lib/libc/libc.c;   LIBC=$OUT
compile lib/strawx/strawx.c; STRAWX=$OUT
compile lib/net/netlib.c "${NET[@]}"; NETLIB=$OUT
BASE=($CRT0 $LIBC)

compile src/init.c;        INIT=$OUT
compile src/lsh.c -Ilib/lua; LSH=$OUT
compile lib/lua/onelua.c -DMAKE_LIB -Ilib/lua; LUA=$OUT
compile src/strawx/main.c; SXMAIN=$OUT
TG=obj/tccgen
if stale $TG/tccrun.c lib/tinycc/tccrun.c lib/tinycc/tcc.c lib/tinycc/libtcc.c build.sh; then
    mkdir -p $TG
    cp lib/tinycc/tcc.c lib/tinycc/libtcc.c $TG/
    awk '/^# include <sys\/mman.h>$/ { next }
         /VirtualProtect\(ptr/ { win = 1 }
         win && /^#else$/ { print; print "    (void)ptr; (void)length; (void)mode;"; skip = 1; win = 0; next }
         skip { if (/^#endif$/) { skip = 0; print } next }
         { print }' lib/tinycc/tccrun.c > $TG/tccrun.c
fi
compile $TG/tcc.c -DONE_SOURCE=1 -Ilib/tinycc-port -Ilib/tinycc -w; TCC=$OUT
compile src/fetch.c "${NET[@]}"; FETCH=$OUT

DISCORD=()
for src in src/discord/*.c; do
    compile "$src" "${NET[@]}"
    DISCORD+=($OUT)
done

APPS=()
for app in files notes paint calc clock sysmon cube viewer settings terminal; do
    if [ -f src/apps/$app.c ]; then
        compile src/apps/$app.c
        APPS+=("$app:$OUT")
    fi
done

wait_all

link bin/init.elf    "${BASE[@]}" $INIT
link bin/lsh.elf     "${BASE[@]}" $LSH $LUA
link bin/strawx.elf  "${BASE[@]}" $SXMAIN $STRAWX
link bin/tcc.elf     "${BASE[@]}" $TCC
link bin/fetch.elf   "${BASE[@]}" $FETCH $STRAWX $NETLIB obj/libbearssl.a
link bin/netsurf.elf "${BASE[@]}" obj/libnetsurf.a $STRAWX $NETLIB obj/libbearssl.a
link bin/discord.elf "${BASE[@]}" "${DISCORD[@]}" $STRAWX $NETLIB obj/libbearssl.a
link bin/lite-xl.elf "${BASE[@]}" obj/liblitexl.a obj/libfreetype.a obj/libpcre2.a $LUA $STRAWX
for entry in "${APPS[@]}"; do
    link bin/${entry%%:*}.elf "${BASE[@]}" ${entry#*:} $STRAWX
done

wait_all
