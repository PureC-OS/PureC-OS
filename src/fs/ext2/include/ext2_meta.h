#pragma once

#include <stdbool.h>
#include <stdint.h>

#define EXT2_META_KEEP 0xFFFFFFFFu

struct ext2_meta {
  uint16_t mode;
  bool directory;
  uint32_t uid;
  uint32_t gid;
};

int32_t ext2_meta_get(const char *path, struct ext2_meta *out);
int32_t ext2_meta_set(const char *path, int32_t mode, uint32_t uid,
                      uint32_t gid);
int32_t ext2_unlink_path(const char *path);
int32_t ext2_rename_path(const char *path, const char *new_name);
int32_t ext2_move_path(const char *path, const char *destination_directory);
