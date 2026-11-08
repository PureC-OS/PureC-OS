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

# No native ctype rune table: use libc++'s built-in classification table (library and users must agree).
SITE="$SRC/libcxx/include/__config_site.in"
if ! grep -q "_LIBCPP_PROVIDES_DEFAULT_RUNE_TABLE" "$SITE"; then
  sed -i '/^#endif \/\/ _LIBCPP___CONFIG_SITE/i #define _LIBCPP_PROVIDES_DEFAULT_RUNE_TABLE' "$SITE"
fi
grep -q "_LIBCPP_PROVIDES_DEFAULT_RUNE_TABLE" "$SITE" || { echo "mk/cxx/patch.sh: __config_site.in was not patched" >&2; exit 1; }
