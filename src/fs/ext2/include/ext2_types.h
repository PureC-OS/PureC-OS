#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "../../../drivers/storage/block_device.h"

#define EXT2_MAGIC 0xEF53
#define EXT2_ROOT_INO 2
#define EXT2_S_IFDIR 0x4000
#define EXT2_S_IFREG 0x8000
#define EXT2_FT_DIR 2
#define EXT2_FT_REG_FILE 1
#define EXT2_MAX_OPEN 16
#define EXT2_DESCRIPTOR_BASE 3
#define EXT2_NAME_MAX 255

struct ext2_volume {
    uint32_t partition_lba;
    uint32_t block_size;
    uint32_t blocks_per_group;
    uint32_t inodes_per_group;
    uint32_t total_blocks;
    uint32_t total_inodes;
    uint32_t first_data_block;
    uint32_t groups_count;
    uint32_t inodes_per_block;
    uint16_t inode_size;
    bool mounted;
};

struct ext2_handle {
    bool used;
    uint32_t inode;
    uint32_t size;
    uint32_t position;
};
