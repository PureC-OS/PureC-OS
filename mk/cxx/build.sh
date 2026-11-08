#!/bin/sh
# Build libc++ and libc++abi for PureC OS against src/libc (static, no host libc involved).
# Features are toggled with the CXX_* variables below and are meant to be switched on
# one at a time as the PureC libc grows (threads, wide chars, locale, exceptions).
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BIN_DIR="${BIN_DIR:-$ROOT/bin}"
SRC="${CXX_LIBS_DIR:-$ROOT/cxx-libs}/llvm-project"
BUILD="$BIN_DIR/cxx-build"
STAGE="${CXX_STAGE:-$BIN_DIR/cxx-stage}"

CXX_THREADS="${CXX_THREADS:-OFF}"
CXX_EXCEPTIONS="${CXX_EXCEPTIONS:-OFF}"
CXX_LOCALIZATION="${CXX_LOCALIZATION:-OFF}"
CXX_WIDE_CHARACTERS="${CXX_WIDE_CHARACTERS:-OFF}"
CXX_FILESYSTEM="${CXX_FILESYSTEM:-OFF}"
CXX_RANDOM_DEVICE="${CXX_RANDOM_DEVICE:-OFF}"

[ -d "$SRC/runtimes" ] || { echo "LLVM sources missing: run mk/cxx/fetch.sh" >&2; exit 1; }

RESOURCE_INCLUDE="$(clang --print-resource-dir)/include"
LIBC_INCLUDE="$ROOT/src/libc/include/hosted"
TARGET="x86_64-unknown-elf"
COMMON="--target=$TARGET -m64 -mno-red-zone -fno-stack-protector -fno-pic -mcmodel=small -nostdinc -isystem $RESOURCE_INCLUDE -isystem $LIBC_INCLUDE"

cmake -G Ninja -S "$SRC/runtimes" -B "$BUILD" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_SYSTEM_NAME=Generic \
  -DCMAKE_SYSTEM_PROCESSOR=x86_64 \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_ASM_COMPILER=clang \
  -DCMAKE_C_COMPILER_TARGET="$TARGET" -DCMAKE_CXX_COMPILER_TARGET="$TARGET" \
  -DCMAKE_AR="$(command -v llvm-ar)" -DCMAKE_RANLIB="$(command -v llvm-ranlib)" \
  -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
  -DCMAKE_C_FLAGS="$COMMON" -DCMAKE_CXX_FLAGS="$COMMON" \
  -DCMAKE_INSTALL_PREFIX="$STAGE" \
  -DLLVM_ENABLE_RUNTIMES="libcxx;libcxxabi" \
  -DLLVM_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_DOCS=OFF \
  -DLIBCXX_ENABLE_SHARED=OFF -DLIBCXXABI_ENABLE_SHARED=OFF \
  -DLIBCXX_ENABLE_STATIC=ON -DLIBCXXABI_ENABLE_STATIC=ON \
  -DLIBCXX_ENABLE_STATIC_ABI_LIBRARY=ON -DLIBCXX_CXX_ABI=libcxxabi \
  -DLIBCXXABI_USE_LLVM_UNWINDER=OFF \
  -DLIBCXX_ENABLE_THREADS="$CXX_THREADS" -DLIBCXXABI_ENABLE_THREADS="$CXX_THREADS" \
  -DLIBCXX_ENABLE_EXCEPTIONS="$CXX_EXCEPTIONS" -DLIBCXXABI_ENABLE_EXCEPTIONS="$CXX_EXCEPTIONS" \
  -DLIBCXX_ENABLE_LOCALIZATION="$CXX_LOCALIZATION" \
  -DLIBCXX_ENABLE_WIDE_CHARACTERS="$CXX_WIDE_CHARACTERS" \
  -DLIBCXX_ENABLE_FILESYSTEM="$CXX_FILESYSTEM" \
  -DLIBCXX_ENABLE_RANDOM_DEVICE="$CXX_RANDOM_DEVICE" \
  -DLIBCXX_ENABLE_UNICODE=OFF -DLIBCXX_ENABLE_TIME_ZONE_DATABASE=OFF \
  -DLIBCXX_ENABLE_MONOTONIC_CLOCK=OFF \
  -DLIBCXX_ENABLE_EXPERIMENTAL_LIBRARY=OFF \
  -DLIBCXX_INCLUDE_TESTS=OFF -DLIBCXX_INCLUDE_BENCHMARKS=OFF -DLIBCXXABI_INCLUDE_TESTS=OFF \
  -DLIBCXX_HARDENING_MODE=none

cmake --build "$BUILD" --target cxx cxxabi
cmake --install "$BUILD" --component cxx --component cxxabi 2>/dev/null || cmake --install "$BUILD"
echo "libc++ staged in $STAGE"
