#pragma once

#include "vfs_ops.h"
#include "vfs_path.h"
#include <stdbool.h>
#include <stdint.h>

#define VFS_MOUNT_MAX 16
#define VFS_MOUNT_READ_ONLY 1U

struct vfs_mount {
  char path[VFS_PATH_MAX];
  char source[64];
  const struct vfs_fs_ops *ops;
  void *context;
  uint32_t flags;
  bool used;
};

int32_t vfs_mount_add(const char *path, const struct vfs_fs_ops *ops,
                      void *context, const char *source, uint32_t flags);
int32_t vfs_mount_replace(const char *path, const struct vfs_fs_ops *ops,
                          void *context, const char *source, uint32_t flags);
int32_t vfs_mount_remove(const char *path);
struct vfs_mount *vfs_mount_find(const char *path, const char **relative,
                                 char *relative_buffer, uint32_t capacity);
struct vfs_mount *vfs_mount_at(const char *path);
uint32_t vfs_mount_count(void);
const struct vfs_mount *vfs_mount_get(uint32_t index);
uint32_t vfs_mount_children(const char *directory, const char **names,
                            uint32_t capacity);
void vfs_mount_reset(void);
