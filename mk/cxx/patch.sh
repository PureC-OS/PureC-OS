#!/bin/sh
# Idempotent PureC OS adjustments to the fetched LLVM sources (cxx-libs/ is not tracked by git).
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SRC="${CXX_LIBS_DIR:-$ROOT/cxx-libs}/llvm-project"

# libc++ has no locale backend for PureC OS: reuse the single-locale (Fuchsia-style) one.
FILE="$SRC/libcxx/include/__locale_dir/locale_base_api.h"
if ! grep -q "__purec__" "$FILE"; then
  sed -i 's/#  elif defined(__Fuchsia__)$/#  elif defined(__Fuchsia__) || defined(__purec__)/' "$FILE"
fi
grep -q "__purec__" "$FILE" || { echo "mk/cxx/patch.sh: locale_base_api.h was not patched" >&2; exit 1; }
