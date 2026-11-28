#pragma once

#include "vfs_ops.h"
#include <stdbool.h>
#include <stdint.h>

void vfs_fat32_mount_boot(void);
bool vfs_fat32_mount_boot_device(uint32_t device);
bool vfs_fat32_open_device(uint32_t device, const struct vfs_fs_ops **ops,
                           void **context);
