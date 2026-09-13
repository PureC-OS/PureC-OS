#pragma once
// Read-only ramdisk: ustar archive passed by Limine as /boot/initramfs.tar.
//
// Linux-style initramfs role: factory copy of every program, library and
// asset. Resolution order everywhere is disk (VFS root) first, ramdisk
// second: an installed system uses its own fresh files, a live boot
// falls back to the ramdisk.
#include <stdint.h>
#include <stdbool.h>

// Parse the Limine initramfs module and build the file index.
// Safe to call when the module is missing (ramdisk stays empty).
// Needs klog only; call any time after early boot.
bool ramdisk_init(void);
bool ramdisk_is_ready(void);

// Look up an absolute path ("/bin/init"). Returns false when missing.
// Data points straight into the Limine module image (do not free).
bool ramdisk_file(const char *path, const void **data, uint64_t *size);
uint32_t ramdisk_file_count(void);
// Dump the index via klog (debug).
void ramdisk_list(void);
