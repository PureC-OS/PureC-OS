#pragma once

#include <stdint.h>

#define EXT2_INODE_ATIME 8
#define EXT2_INODE_CTIME 12
#define EXT2_INODE_MTIME 16
#define EXT2_INODE_DTIME 20
#define EXT2_INODE_LINKS 26

uint32_t ext2_now(void);
void ext2_stamp_new(uint8_t *inode);
void ext2_stamp_modified(uint8_t *inode);
void ext2_stamp_changed(uint8_t *inode);
