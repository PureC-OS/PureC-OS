#pragma once

#include <stdbool.h>
#include <stdint.h>

#define EXT2_META_KEEP 0xFFFFFFFFu

struct ext2_meta {
  uint16_t mode;
  bool directory;
  bool symlink;
  uint32_t uid;
  uint32_t gid;
  uint32_t size;
  uint32_t atime;
  uint32_t mtime;
  uint32_t ctime;
};

int32_t ext2_meta_get(const char *path, struct ext2_meta *out);
int32_t ext2_meta_set_times(const char *path, uint32_t atime, uint32_t mtime);
int32_t ext2_meta_set(const char *path, int32_t mode, uint32_t uid,
                      uint32_t gid);
int32_t ext2_unlink_path(const char *path);
int32_t ext2_symlink_path(const char *path, const char *target);
int32_t ext2_readlink_path(const char *path, char *out, uint32_t capacity);
int32_t ext2_link_walk(const char *path, bool follow_last, char *target,
                       uint32_t capacity, uint32_t *consumed);
bool ext2_inode_is_fast_symlink(const uint8_t *inode);
void ext2_orphans_cleanup(void);
int32_t ext2_inode_release_if_orphan(uint32_t ino);
int32_t ext2_rename_path(const char *path, const char *new_name);
int32_t ext2_move_path(const char *path, const char *destination_directory);
