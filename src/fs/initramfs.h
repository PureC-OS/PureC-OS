#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "types/fs_types.h"

bool initramfs_mount(void);
bool initramfs_is_mounted(void);
bool initramfs_find(const char *path, const void **data, uint32_t *size);
int32_t initramfs_list(const char *path, struct fs_directory_entry *entries,
                       uint32_t capacity);
int32_t initramfs_list_long(const char *path,
                            struct fs_directory_entry_long *entries,
                            uint32_t capacity);
