#pragma once

#include <stdbool.h>
#include <stdint.h>

bool ext2_super_read(uint32_t partition_lba);
bool ext2_super_mount_at(uint32_t lba);
bool ext2_super_get_u32(uint32_t offset, uint32_t *value);
bool ext2_super_set_u32(uint32_t offset, uint32_t value);
