#!/bin/sh
# Apply the PureC OS patch to libcss: CSS custom properties (--name) and var().
# Idempotent: does nothing when the patch is already applied.
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
LIB="${NSLIBS:-$ROOT/netsurf-libs}/libcss"
PATCH="$ROOT/mk/netsurf/patches/libcss-custom-properties.patch"
[ -d "$LIB/.git" ] || { echo "libcss sources missing: run mk/netsurf/fetch.sh" >&2; exit 1; }
if git -C "$LIB" apply --reverse --check "$PATCH" >/dev/null 2>&1; then
  exit 0
fi
git -C "$LIB" apply --check "$PATCH" || { echo "libcss patch does not apply" >&2; exit 1; }
git -C "$LIB" apply "$PATCH" || exit 1
echo "libcss patched (custom properties)"
