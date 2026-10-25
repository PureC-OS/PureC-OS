#pragma once

#include <stdbool.h>
#include <stdint.h>

bool ext2_super_read(uint32_t partition_lba);
bool ext2_super_mount_at(uint32_t lba);
