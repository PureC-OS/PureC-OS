#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "../../../kernel/syscall/syscall.h"

int32_t ext2_stat_path(const char *path, struct ext2_stat_info *out);
int32_t ext2_stat_ino(uint32_t ino, struct ext2_stat_info *out);
int32_t ext2_super_info(struct ext2_super_info *out);
int32_t ext2_file_blocks(const char *path, struct ext2_blocks_info *out);
int32_t ext2_inode_blocks(uint32_t ino, struct ext2_blocks_info *out);
