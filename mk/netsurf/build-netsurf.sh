#!/bin/sh
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
STAGE="$ROOT/bin/ns-stage"
H="$ROOT/src/libc/include/hosted"
LOG="$STAGE/netsurf-core.log"
export PKG_CONFIG_LIBDIR="$STAGE/lib/pkgconfig"
export PKG_CONFIG_PATH=
mkdir -p "$STAGE"
cd "$ROOT/netsurf" || exit 1
make -k -j8 TARGET=framebuffer NETSURF_FB_FRONTEND=sdl NETSURF_USE_DUKTAPE=NO \
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
