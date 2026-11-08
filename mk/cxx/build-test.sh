#!/bin/sh
# Compile and link tests/cxx/hello.cpp against the staged libc++ (run mk/cxx/build.sh first).
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BIN_DIR="${BIN_DIR:-$ROOT/bin}"
STAGE="${CXX_STAGE:-$BIN_DIR/cxx-stage}"
OUT="$BIN_DIR/cxx-test"
RESOURCE_INCLUDE="$(clang --print-resource-dir)/include"
mkdir -p "$OUT"
[ $# -gt 0 ] || set -- "$ROOT/tests/cxx/hello.cpp"
for source in "$@"; do
  name="$(basename "$source" .cpp)"
  clang++ --target=x86_64-unknown-elf -std=c++20 -O1 -fno-exceptions -m64 -mno-red-zone \
    -fno-stack-protector -fno-pic -mcmodel=small -nostdinc -nostdinc++ \
    -isystem "$STAGE/include/c++/v1" -isystem "$RESOURCE_INCLUDE" -isystem "$ROOT/src/libc/include/hosted" \
    -c "$source" -o "$OUT/$name.o"
  x86_64-elf-ld -T "$ROOT/linker-userspace.ld" -o "$OUT/$name" "$BIN_DIR/lib/crt0.o" "$OUT/$name.o" \
    "$STAGE/lib/libc++.a" "$STAGE/lib/libc++abi.a" "$BIN_DIR/lib/libpurefs.a" "$BIN_DIR/lib/libpurec.a" \
    2>&1 | grep -v "GNU-stack\|deprecated" || true
  echo "built $OUT/$name"
done
