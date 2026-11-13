#!/bin/sh
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DEST="$ROOT/netsurf-libs"
BASE="git://git.netsurf-browser.org"
LIBS="buildsystem libwapcaplet libparserutils libhubbub libcss libdom libnsgif libnsbmp libnsutils libnslog libnspsl libnsfb libutf8proc libsvgtiny"
mkdir -p "$DEST"
for lib in $LIBS; do
  if [ -d "$DEST/$lib" ]; then continue; fi
  git clone --depth 1 "$BASE/$lib.git" "$DEST/$lib"
done
if [ ! -d "$DEST/zlib" ]; then
  git clone --depth 1 --branch v1.3.1 https://github.com/madler/zlib "$DEST/zlib"
fi
if [ ! -d "$DEST/libpng" ]; then
  git clone --depth 1 --branch v1.6.47 https://github.com/pnggroup/libpng "$DEST/libpng"
fi
if [ ! -d "$DEST/libjpeg-turbo" ]; then
  git clone --depth 1 --branch 2.1.5.1 https://github.com/libjpeg-turbo/libjpeg-turbo "$DEST/libjpeg-turbo"
fi
if [ ! -d "$DEST/libexpat" ]; then
  git clone --depth 1 --branch R_2_6_4 https://github.com/libexpat/libexpat "$DEST/libexpat"
fi
if [ ! -f "$DEST/unifont.hex" ]; then
  curl -sL https://unifoundry.com/pub/unifont/unifont-16.0.01/font-builds/unifont-16.0.01.hex.gz | gzip -dc > "$DEST/unifont.hex"
fi
for lib in $LIBS zlib libpng libjpeg-turbo libexpat; do
  [ -d "$DEST/$lib/.git" ] && echo "$lib $(git -C "$DEST/$lib" rev-parse HEAD)"
done > "$ROOT/mk/netsurf/versions.txt"
