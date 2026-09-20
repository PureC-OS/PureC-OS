#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "types/fs_types.h"

#define INITRAMFS_BOOT_PATH "/boot/initramfs.cpio"
#define INITRAMFS_ESP_ALIAS "/boot/initra~1.cpi"
#define INITRAMFS_ESP_ALIAS_NAME "INITRA~1.CPI"
bool initramfs_boot_image(const void **data, uint64_t *size);
bool initramfs_mount(void);
bool initramfs_is_mounted(void);
bool initramfs_find(const char *path, const void **data, uint32_t *size);
int32_t initramfs_list(const char *path, struct fs_directory_entry *entries,
                       uint32_t capacity);
int32_t initramfs_list_long(const char *path,
                            struct fs_directory_entry_long *entries,
                            uint32_t capacity);
