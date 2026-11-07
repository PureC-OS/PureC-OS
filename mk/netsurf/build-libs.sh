#!/bin/sh
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
STAGE="$ROOT/bin/ns-stage"
NSLIBS="$ROOT/netsurf-libs"
export PKG_CONFIG_LIBDIR="$STAGE/lib/pkgconfig"
export PKG_CONFIG_PATH=
export CFLAGS="-g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mno-red-zone -mcmodel=small -I$ROOT/src/libc/include/hosted -I$STAGE/include $NS_EXTRA_CFLAGS"
mkdir -p "$STAGE"
if [ ! -d "$STAGE/share/netsurf-buildsystem" ]; then
  make -C "$NSLIBS/buildsystem" install PREFIX="$STAGE" >/dev/null || exit 1
fi
LIBS="${NS_LIBS:-libwapcaplet libparserutils libhubbub libcss libdom libnsutils libnslog libnsgif libnsbmp libnsfb}"
for lib in $LIBS; do
  echo "== $lib"
  if ! make -C "$NSLIBS/$lib" PREFIX="$STAGE" CC=x86_64-elf-gcc AR=x86_64-elf-ar WARNFLAGS=-w \
       COMPONENT_TYPE=lib-static install > "$STAGE/$lib.log" 2>&1; then
    echo "FAILED $lib (log: $STAGE/$lib.log)"
    tail -5 "$STAGE/$lib.log"
    [ -n "$NS_KEEP_GOING" ] || exit 1
  fi
done
