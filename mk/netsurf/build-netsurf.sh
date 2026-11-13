#!/bin/sh
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
STAGE="$ROOT/bin/ns-stage"
H="$ROOT/src/libc/include/hosted"
LOG="$STAGE/netsurf-core.log"
export PKG_CONFIG_LIBDIR="$STAGE/lib/pkgconfig"
export PKG_CONFIG_PATH=
mkdir -p "$STAGE"
GB="$ROOT/netsurf-libs/nsgenbind"
if [ ! -x "$STAGE/host/bin/nsgenbind" ]; then
  [ -d "$GB" ] || git clone --depth 1 git://git.netsurf-browser.org/nsgenbind.git "$GB" || exit 1
  make -C "$GB" NSSHARED="$ROOT/netsurf-libs/buildsystem" -j8 > "$STAGE/nsgenbind.log" 2>&1 || { echo "nsgenbind: build failed, see $STAGE/nsgenbind.log"; exit 1; }
  mkdir -p "$STAGE/host/bin"
  cp "$GB"/build-*/nsgenbind "$STAGE/host/bin/nsgenbind" || exit 1
fi
export PATH="$STAGE/host/bin:$PATH"
FONT_ARG=""
if [ -f "$ROOT/netsurf-libs/unifont.hex" ]; then
  python3 "$ROOT/mk/netsurf/gen-glyph-data.py" \
    "$ROOT/netsurf/frontends/framebuffer/res/fonts/glyph_data" \
    "$ROOT/netsurf-libs/unifont.hex" "$STAGE/glyph_data" > /dev/null || exit 1
  FONT_ARG="FB_FONT_internal_ns-sans=../../../../bin/ns-stage/glyph_data"
fi
cd "$ROOT/netsurf" || exit 1
make -k -j8 $FONT_ARG TARGET=framebuffer NETSURF_FB_FRONTEND=sdl NETSURF_USE_DUKTAPE=YES \
  NETSURF_USE_OPENSSL=NO NETSURF_USE_NSPSL=NO NETSURF_USE_WEBP=NO NETSURF_USE_JPEGXL=NO \
  NETSURF_USE_VIDEO=NO NETSURF_USE_HARU_PDF=NO NETSURF_USE_NSSVG=NO NETSURF_USE_ROSPRITE=NO \
  NETSURF_FB_FONTLIB=internal \
  CC="x86_64-elf-gcc -g -ffreestanding -fno-stack-protector -fno-pic -m64 -mno-red-zone -mcmodel=small -w -D_GNU_SOURCE -I$H -I$STAGE/include/libutf8proc" "$@" > "$LOG" 2>&1
status=$?
if [ "$1" = "clean" ]; then
  exit $status
fi
if grep -E 'error:|fatal error' "$LOG" | grep -v 'ld returned' > /dev/null; then
  grep -E 'error:|fatal error' "$LOG" | grep -v 'ld returned' | head -20
  echo "netsurf core: compile errors, see $LOG"
  exit 1
fi
echo "netsurf core: objects built (NetSurf's own link step is replaced by mk/netsurf/Makefile)"
exit 0
