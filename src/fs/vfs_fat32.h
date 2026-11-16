#pragma once

#include <stdbool.h>
#include <stdint.h>

void vfs_fat32_mount_boot(void);
bool vfs_fat32_mount_boot_device(uint32_t device);
