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
for lib in $LIBS; do
  [ -d "$DEST/$lib/.git" ] && echo "$lib $(git -C "$DEST/$lib" rev-parse HEAD)"
done > "$ROOT/mk/netsurf/versions.txt"
