#!/usr/bin/env bash
set -euo pipefail

readonly TARGET="x86_64-elf"
readonly BINUTILS_VERSION="2.42"
readonly GCC_VERSION="14.1.0"
readonly BINUTILS_SHA256="f6e4d41fd5fc778b06b7891457b3620da5ecea1006c6a4a41ae998109f85a800"
readonly GCC_SHA256="e283c654987afe3de9d8080bc0bd79534b5ca0d681a73a11ff2b5d3767426840"
readonly PREFIX="${CROSS_PREFIX:-${HOME}/cross}"
readonly JOBS="${JOBS:-$(nproc)}"
readonly MANIFEST="${PREFIX}/.purec-toolchain-version"
readonly EXPECTED="${TARGET} binutils-${BINUTILS_VERSION} gcc-${GCC_VERSION}"

if [[ -x "${PREFIX}/bin/${TARGET}-gcc" && -f "${MANIFEST}" ]] &&
   [[ "$(<"${MANIFEST}")" == "${EXPECTED}" ]]; then
    echo "Cross toolchain is already installed: ${EXPECTED}"
    exit 0
fi

for command in curl sha256sum tar make gcc g++; do
    command -v "${command}" >/dev/null || {
        echo "Missing host dependency: ${command}" >&2
        exit 1
    }
done

work_dir="$(mktemp -d)"
trap 'rm -rf "${work_dir}"' EXIT
rm -rf "${PREFIX}"
mkdir -p "${PREFIX}"

download_and_verify() {
    local url="$1"
    local output="$2"
    local checksum="$3"
    curl --fail --location --retry 3 --output "${output}" "${url}"
    printf '%s  %s\n' "${checksum}" "${output}" | sha256sum --check --status
}

cd "${work_dir}"
download_and_verify \
    "https://ftp.gnu.org/gnu/binutils/binutils-${BINUTILS_VERSION}.tar.xz" \
    "binutils-${BINUTILS_VERSION}.tar.xz" "${BINUTILS_SHA256}"
tar -xf "binutils-${BINUTILS_VERSION}.tar.xz"
mkdir build-binutils
cd build-binutils
"../binutils-${BINUTILS_VERSION}/configure" \
    --target="${TARGET}" \
    --prefix="${PREFIX}" \
    --with-sysroot \
    --disable-nls \
    --disable-werror
make -j"${JOBS}"
make install

cd "${work_dir}"
download_and_verify \
    "https://ftp.gnu.org/gnu/gcc/gcc-${GCC_VERSION}/gcc-${GCC_VERSION}.tar.xz" \
    "gcc-${GCC_VERSION}.tar.xz" "${GCC_SHA256}"
tar -xf "gcc-${GCC_VERSION}.tar.xz"
mkdir build-gcc
cd build-gcc
"../gcc-${GCC_VERSION}/configure" \
    --target="${TARGET}" \
    --prefix="${PREFIX}" \
    --disable-nls \
    --disable-multilib \
    --enable-languages=c,c++ \
    --without-headers
make -j"${JOBS}" all-gcc
make install-gcc
make -j"${JOBS}" all-target-libgcc
make install-target-libgcc

printf '%s\n' "${EXPECTED}" >"${MANIFEST}"
"${PREFIX}/bin/${TARGET}-gcc" --version | head -n 1
"${PREFIX}/bin/${TARGET}-ld" --version | head -n 1
