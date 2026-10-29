#!/bin/sh
# Builds a ready-to-copy FAT32 EFI System Partition image for the installer.
# The kernel carries no FAT code: it writes this image to the ESP raw.
#
# usage: make_esp_image.sh <iso_root> <output.img>
set -eu

iso_root=${1:?usage: make_esp_image.sh <iso_root> <output.img>}
out=${2:?usage: make_esp_image.sh <iso_root> <output.img>}

for tool in mkfs.fat mmd mcopy; do
    command -v "$tool" >/dev/null 2>&1 || { echo "make_esp_image: $tool not found (dosfstools/mtools)" >&2; exit 1; }
done

files="boot/kernel.elf boot/kernel-fallback.elf boot/initramfs.cpio EFI/BOOT/BOOTX64.EFI"
need=0
for f in $files; do
    [ -f "$iso_root/$f" ] || { echo "make_esp_image: missing $iso_root/$f" >&2; exit 1; }
    need=$((need + $(stat -c %s "$iso_root/$f")))
done

# content + 15% FAT/slack + 8 MiB, rounded up to whole MiB, at least 64 MiB
mib=$(( (need + need / 7 + 8 * 1048576 + 1048575) / 1048576 ))
[ "$mib" -ge 64 ] || mib=64

conf=$(mktemp)
trap 'rm -f "$conf"' EXIT
cat > "$conf" <<'CFG'
timeout: 10
verbose: yes

/PureC OS (installed)
    protocol: limine
    resolution: 1920x1080x32
    kernel_path: boot():/boot/kernel.elf
    module_path: boot():/boot/kernel-fallback.elf
    module_path: boot():/EFI/BOOT/BOOTX64.EFI
    module_path: boot():/boot/initramfs.cpio

/PureC OS (fallback previous image)
    protocol: limine
    resolution: 1280x800x32
    kernel_path: boot():/boot/kernel-fallback.elf
    module_path: boot():/EFI/BOOT/BOOTX64.EFI
    module_path: boot():/boot/initramfs.cpio
CFG

rm -f "$out"
truncate -s "${mib}M" "$out"
mkfs.fat -F 32 -S 512 -n PUREC-ESP "$out" >/dev/null

mmd -i "$out" ::/EFI ::/EFI/BOOT ::/boot ::/boot/limine
mcopy -i "$out" "$iso_root/EFI/BOOT/BOOTX64.EFI"    ::/EFI/BOOT/BOOTX64.EFI
mcopy -i "$out" "$iso_root/boot/kernel.elf"          ::/boot/kernel.elf
mcopy -i "$out" "$iso_root/boot/kernel-fallback.elf" ::/boot/kernel-fallback.elf
mcopy -i "$out" "$iso_root/boot/initramfs.cpio"      ::/boot/initramfs.cpio
for dest in ::/limine.conf ::/boot/limine/limine.conf ::/EFI/BOOT/limine.conf; do
    mcopy -i "$out" "$conf" "$dest"
done

echo "make_esp_image: $out (${mib} MiB)"
