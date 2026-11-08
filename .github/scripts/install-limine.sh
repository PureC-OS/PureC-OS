#!/usr/bin/env bash
set -euo pipefail

readonly LIMINE_VERSION="12.9.0"
readonly LIMINE_SHA256="84059c93b4ea03994af6d614654c7095291388850ea7b258d64f9263abde5557"
readonly ARCHIVE_URL="https://github.com/Limine-Bootloader/Limine/releases/download/v${LIMINE_VERSION}/limine-binary.tar.gz"
readonly DESTINATION="${LIMINE_DESTINATION:-/tmp/limine-pkg/usr}"

work_dir="$(mktemp -d)"
trap 'rm -rf "${work_dir}"' EXIT
archive="${work_dir}/limine-binary.tar.gz"

curl --fail --location --retry 3 --output "${archive}" "${ARCHIVE_URL}"
printf '%s  %s\n' "${LIMINE_SHA256}" "${archive}" | sha256sum --check --status
tar -xf "${archive}" -C "${work_dir}"

source_dir="${work_dir}/limine-binary"
test -d "${source_dir}"
make -C "${source_dir}" -j"$(nproc)"

mkdir -p "${DESTINATION}/bin" "${DESTINATION}/share/limine"
install -m 0755 "${source_dir}/limine" "${DESTINATION}/bin/limine"
for file in limine-bios.sys limine-bios-cd.bin limine-uefi-cd.bin \
            BOOTX64.EFI BOOTIA32.EFI; do
    if [[ -f "${source_dir}/${file}" ]]; then
        install -m 0644 "${source_dir}/${file}" \
            "${DESTINATION}/share/limine/${file}"
    fi
done

test -x "${DESTINATION}/bin/limine"
test -f "${DESTINATION}/share/limine/limine-bios-cd.bin"
"${DESTINATION}/bin/limine" --version | head -n 1
