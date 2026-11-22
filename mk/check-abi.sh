#!/bin/sh
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
A="$ROOT/programs/PureC-OS-Core-Programs/libc/include/purec/abi"
K="$ROOT/src"
W=${TMPDIR:-/tmp}/purec-abi-check.$$
mkdir -p "$W"
trap 'rm -rf "$W"' EXIT

pairs="syscall.h:kernel/syscall/syscall.h fs_types.h:fs/types/fs_types.h key_event.h:drivers/input/key_event.h keyboard.h:drivers/input/keyboard.h storage_types.h:io/storage/storage_types.h mouse.h:drivers/mouse/ps2_mouse.h"

probe() {
    inc=$1; hdr=$2; out=$3
    {
        echo '#include <stdio.h>'
        echo '#include <stddef.h>'
        echo "#include \"$hdr\""
        echo 'int main(void){'
        grep -E '^#define [A-Z0-9_]+ +(\(?-?[0-9]|0x|[A-Z])' "$hdr" | awk '{print $2}' | sort -u | while read n; do
            echo "#ifdef $n"
            echo "printf(\"%s=%lld\\n\", \"$n\", (long long)($n));"
            echo "#endif"
        done
        grep -E '^struct [a-z_0-9]+ *\{?$' "$hdr" | awk '{print $2}' | sort -u | while read s; do
            echo "printf(\"sizeof(struct $s)=%zu align=%zu\\n\", sizeof(struct $s), _Alignof(struct $s));"
        done
        echo 'return 0;}'
    } > "$W/$out.c"
    cc -std=gnu11 -w -I"$K" -o "$W/$out" "$W/$out.c"
    "$W/$out" | sort > "$W/$out.txt"
}

for p in $pairs; do
    a=${p%%:*}; k=${p#*:}
    probe "$A" "$A/$a" "abi_$a"
    probe "$K" "$K/$k" "ker_$a"
    awk '/^#define /{print}' "$K/$k" | sort > "$W/ker_$a.defs"
    awk '/^#define /{print}' "$A/$a" | sort > "$W/abi_$a.defs"
    n=$(wc -l < "$W/abi_$a.txt")
    comm -23 "$W/abi_$a.txt" "$W/ker_$a.txt" > "$W/d.txt"
    if [ -s "$W/d.txt" ]; then echo "ABI mismatch in $a"; cat "$W/d.txt"; exit 1; fi
    comm -23 "$W/abi_$a.defs" "$W/ker_$a.defs" | grep . && { echo "abi-only defines in $a"; exit 1; } || true
    echo "$a: $n checked values/structs identical"
done
echo "ABI check OK"
