#pragma once

#include <stdbool.h>
#include <stdint.h>

bool ext2_inode_read(uint32_t ino, uint8_t *out);
uint32_t ext2_inode_block(uint32_t ino);
bool ext2_group_desc_read(uint32_t group, uint8_t *out);
uint32_t ext2_inode_block_ptr(const uint8_t *inode, uint32_t logical);
int32_t ext2_inode_read_data(const uint8_t *inode, uint32_t offset, void *buf, uint32_t count);
void ext2_inode_free_blocks(const uint8_t *inode);
