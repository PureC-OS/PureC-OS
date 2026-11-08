#!/bin/sh
# Compile and link C++ test programs against the staged libc++ (run mk/cxx/build.sh first).
# Usage: build-test.sh [source.cpp ...]; default builds every tests/cxx/*.cpp.
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BIN_DIR="${BIN_DIR:-$ROOT/bin}"
STAGE="${CXX_STAGE:-$BIN_DIR/cxx-stage}"
OUT="$BIN_DIR/cxx-test"
RESOURCE_INCLUDE="$(clang --print-resource-dir)/include"
LIBGCC="$(x86_64-elf-gcc -print-libgcc-file-name 2>/dev/null || true)"
CXX_FLAGS="${CXX_FLAGS:--std=c++20 -O1 -fexceptions -frtti}"
mkdir -p "$OUT"
[ $# -gt 0 ] || set -- "$ROOT"/tests/cxx/*.cpp
for source in "$@"; do
  name="$(basename "$source" .cpp)"
  clang++ --target=x86_64-unknown-elf -D__purec__=1 $CXX_FLAGS -m64 -mno-red-zone \
    -fno-stack-protector -fno-pic -mcmodel=small -nostdinc -nostdinc++ \
    -isystem "$STAGE/include/c++/v1" -isystem "$RESOURCE_INCLUDE" -isystem "$ROOT/src/libc/include/hosted" \
    -c "$source" -o "$OUT/$name.o"
  x86_64-elf-ld --eh-frame-hdr -T "$ROOT/linker-userspace-cxx.ld" -o "$OUT/$name" "$BIN_DIR/lib/crt0.o" "$OUT/$name.o" \
    "$STAGE/lib/libc++.a" "$STAGE/lib/libc++abi.a" "$BIN_DIR/lib/libpurefs.a" "$BIN_DIR/lib/libpurec.a" $LIBGCC \
    2>&1 | grep -v "GNU-stack\|deprecated" || true
  echo "built $OUT/$name"
done
