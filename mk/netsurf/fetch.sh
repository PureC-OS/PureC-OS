#!/bin/sh
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DEST="$ROOT/netsurf-libs"
BASE="git://git.netsurf-browser.org"
LIBS="libwapcaplet libparserutils libhubbub libcss libdom libnsgif libnsbmp libnsutils libnslog libnspsl libnsfb libutf8proc libsvgtiny"
mkdir -p "$DEST"
for lib in $LIBS; do
  if [ -d "$DEST/$lib" ]; then continue; fi
  git clone --depth 1 "$BASE/$lib.git" "$DEST/$lib"
done
if [ ! -d "$DEST/zlib" ]; then
  git clone --depth 1 --branch v1.3.1 https://github.com/madler/zlib "$DEST/zlib"
fi
if [ ! -f "$DEST/unifont.hex" ]; then
  curl -sL https://unifoundry.com/pub/unifont/unifont-16.0.01/font-builds/unifont-16.0.01.hex.gz | gzip -dc > "$DEST/unifont.hex"
fi
for lib in $LIBS zlib; do
  [ -d "$DEST/$lib/.git" ] && echo "$lib $(git -C "$DEST/$lib" rev-parse HEAD)"
done > "$ROOT/mk/netsurf/versions.txt"
