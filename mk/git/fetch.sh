#!/bin/sh
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DEST="${GIT_SRC_DIR:-$ROOT/git}"
if [ ! -d "$DEST/.git" ]; then
  git clone --depth 1 https://github.com/git/git.git "$DEST"
fi
echo "git $(git -C "$DEST" rev-parse HEAD) $(git -C "$DEST" describe --tags 2>/dev/null)" > "$ROOT/mk/git/versions.txt"
