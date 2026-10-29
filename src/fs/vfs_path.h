#pragma once

#include <stdbool.h>
#include <stdint.h>

#define VFS_PATH_MAX 512
#define VFS_NAME_MAX 255

int32_t vfs_path_normalize(const char *path, char *out, uint32_t capacity);
bool vfs_path_has_prefix(const char *path, const char *prefix);
int32_t vfs_path_parent(const char *path, char *out, uint32_t capacity);
const char *vfs_path_basename(const char *path);
