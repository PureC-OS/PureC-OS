#!/usr/bin/env bash
# Download GNU make 4.4.1 into gnumake/ (the port in src/make builds from there).
# Verified by SHA-256; does nothing when gnumake/src/main.c is already present.
set -euo pipefail

readonly VERSION="4.4.1"
readonly SHA256="dd16fb1d67bfab79a72f5e8390735c49e3e8e70b4945a15ab1f81ddb78658fb3"
readonly URL="https://ftp.gnu.org/gnu/make/make-${VERSION}.tar.gz"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly DEST="${GNUMAKE_DIR:-${ROOT}/gnumake}"

if [[ -f "${DEST}/src/main.c" ]]; then
    echo "gnumake sources already present in ${DEST}"
    exit 0
fi

work_dir="$(mktemp -d)"
trap 'rm -rf "${work_dir}"' EXIT
archive="${work_dir}/make-${VERSION}.tar.gz"

curl --fail --location --retry 3 --output "${archive}" "${URL}"
printf '%s  %s\n' "${SHA256}" "${archive}" | sha256sum --check --status
tar -xzf "${archive}" -C "${work_dir}"

mkdir -p "${DEST}"
cp -a "${work_dir}/make-${VERSION}/." "${DEST}/"
test -f "${DEST}/src/main.c"
echo "gnumake ${VERSION} unpacked into ${DEST}"
