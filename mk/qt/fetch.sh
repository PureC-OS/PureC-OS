#!/bin/sh
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DEST="${QT_SRC_DIR:-$ROOT/qt-src}"
TAG="${QT_TAG:-v6.12.0}"
mkdir -p "$DEST"
if [ ! -d "$DEST/qtbase/.git" ]; then
  git clone --depth 1 --branch "$TAG" https://github.com/qt/qtbase.git "$DEST/qtbase"
fi
echo "qtbase $(git -C "$DEST/qtbase" rev-parse HEAD) $TAG" > "$ROOT/mk/qt/versions.txt"
