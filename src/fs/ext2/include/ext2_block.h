#pragma once

#include <stdbool.h>
#include <stdint.h>

bool ext2_read_block(uint32_t block, void *out);
bool ext2_write_block(uint32_t block, const void *in);
uint8_t *ext2_scratch_block(void);
uint8_t *ext2_scratch_block2(void);
uint8_t *ext2_scratch_block3(void);
uint8_t *ext2_scratch_sector(void);
uint16_t ext2_read_u16(const uint8_t *p);
uint32_t ext2_read_u32(const uint8_t *p);
void ext2_write_u16(uint8_t *p, uint16_t v);
void ext2_write_u32(uint8_t *p, uint32_t v);
struct ext2_volume *ext2_volume(void);
uint32_t ext2_alloc_block(void);
bool ext2_free_block(uint32_t block);
uint32_t ext2_alloc_inode(void);
bool ext2_free_inode(uint32_t ino);
bool ext2_write_inode(uint32_t ino, const uint8_t *in);
bool ext2_group_write(uint32_t group, const uint8_t *gd);
