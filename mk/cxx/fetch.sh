#!/bin/sh
# Fetch the LLVM runtimes (libc++, libc++abi, libunwind) pinned to LLVM_TAG into cxx-libs/.
# Sparse and shallow: only the directories the runtimes build needs.
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DEST="${CXX_LIBS_DIR:-$ROOT/cxx-libs}"
LLVM_TAG="${LLVM_TAG:-llvmorg-23.1.4}"
SRC="$DEST/llvm-project"

mkdir -p "$DEST"
if [ ! -d "$SRC/.git" ]; then
  git clone --depth 1 --branch "$LLVM_TAG" --filter=blob:none --sparse \
    https://github.com/llvm/llvm-project.git "$SRC"
fi
git -C "$SRC" sparse-checkout set --cone \
  runtimes libcxx libcxxabi libunwind libc cmake llvm/cmake llvm/utils third-party
echo "$LLVM_TAG $(git -C "$SRC" rev-parse HEAD)" > "$ROOT/mk/cxx/versions.txt"
cat "$ROOT/mk/cxx/versions.txt"
