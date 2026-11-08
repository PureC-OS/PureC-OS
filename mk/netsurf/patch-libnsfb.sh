#!/bin/sh
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
NSFB="$ROOT/netsurf-libs/libnsfb"
[ -d "$NSFB" ] || exit 1

cp "$ROOT/mk/netsurf/libnsfb/purec.c" "$NSFB/src/surface/purec.c"

if ! grep -q NSFB_SURFACE_PUREC "$NSFB/include/libnsfb.h"; then
  sed -i 's|^    NSFB_SURFACE_RAM, /\*\*< RAM surface \*/|    NSFB_SURFACE_PUREC, /**< PureC OS window surface */\n&|' "$NSFB/include/libnsfb.h"
fi

if ! grep -q 'purec.c' "$NSFB/src/surface/Makefile"; then
  sed -i 's|^SURFACE_HANDLER_yes := surface.c ram.c|& purec.c|' "$NSFB/src/surface/Makefile"
fi

grep -q NSFB_SURFACE_PUREC "$NSFB/include/libnsfb.h" && grep -q 'purec.c' "$NSFB/src/surface/Makefile"
