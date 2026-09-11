#pragma once

#include <stdint.h>
#include "../../types/fs_types.h"

int32_t ext2_dir_find(uint32_t dir_ino, const char *name, uint32_t *out_ino, uint8_t *out_type);
int32_t ext2_dir_resolve(const char *path, uint32_t *out_ino);
int32_t ext2_dir_list(uint32_t dir_ino, struct fs_directory_entry *entries, uint32_t capacity);
int32_t ext2_dir_add_entry(uint32_t dir_ino, const char *name, uint32_t ino, uint8_t file_type);
int32_t ext2_dir_create(uint32_t parent_ino, const char *name);
