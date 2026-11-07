#!/bin/sh
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
STAGE="$ROOT/bin/ns-stage"
H="$ROOT/src/libc/include/hosted"
export PKG_CONFIG_LIBDIR="$STAGE/lib/pkgconfig"
export PKG_CONFIG_PATH=
cd "$ROOT/netsurf" || exit 1
exec make -k -j8 TARGET=framebuffer NETSURF_FB_FRONTEND=sdl NETSURF_USE_DUKTAPE=NO \
  NETSURF_USE_OPENSSL=NO NETSURF_USE_NSPSL=NO NETSURF_USE_WEBP=NO NETSURF_USE_JPEGXL=NO \
  NETSURF_USE_VIDEO=NO NETSURF_USE_HARU_PDF=NO NETSURF_USE_NSSVG=NO NETSURF_USE_ROSPRITE=NO \
  NETSURF_FB_FONTLIB=internal \
  CC="x86_64-elf-gcc -g -ffreestanding -fno-stack-protector -fno-pic -m64 -mno-red-zone -mcmodel=small -w -I$H -I$STAGE/include/libutf8proc" "$@"
