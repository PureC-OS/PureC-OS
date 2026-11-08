#!/bin/sh
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DEST="${LADYBIRD_SRC_DIR:-$ROOT/ladybird-src}"
if [ ! -d "$DEST/.git" ]; then
  git clone --depth 1 https://github.com/LadybirdBrowser/ladybird.git "$DEST"
fi
echo "ladybird $(git -C "$DEST" rev-parse HEAD)" > "$ROOT/mk/ladybird/versions.txt"
