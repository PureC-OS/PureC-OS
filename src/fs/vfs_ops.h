#pragma once

#include "types/fs_types.h"
#include "../kernel/syscall/syscall.h"
#include <stdbool.h>
#include <stdint.h>

struct vfs_perm_meta;

struct vfs_fs_ops {
  const char *name;
  int32_t (*map)(void *context, const char *path, const void **data,
                 uint32_t *size);
  int32_t (*open)(void *context, const char *path);
  int32_t (*read)(void *context, int32_t descriptor, void *buffer,
                  uint32_t count);
  int32_t (*write)(void *context, int32_t descriptor, const void *buffer,
                   uint32_t count);
  int64_t (*seek)(void *context, int32_t descriptor, int64_t offset,
                  uint32_t whence);
  int32_t (*close)(void *context, int32_t descriptor);
  int32_t (*stat)(void *context, const char *path, struct file_stat_info *out);
  int32_t (*list)(void *context, const char *path,
                  struct fs_directory_entry *entries, uint32_t capacity);
  int32_t (*list_long)(void *context, const char *path,
                       struct fs_directory_entry_long *entries,
                       uint32_t capacity);
  int32_t (*create_file)(void *context, const char *path);
  int32_t (*create_directory)(void *context, const char *path);
  int32_t (*unlink)(void *context, const char *path);
  int32_t (*rename)(void *context, const char *path, const char *new_name);
  int32_t (*move)(void *context, const char *path, const char *destination);
  int32_t (*write_file)(void *context, const char *path, const void *buffer,
                        uint32_t count);
  int32_t (*append_file)(void *context, const char *path, const void *buffer,
                         uint32_t count);
  int32_t (*get_meta)(void *context, const char *path,
                      struct vfs_perm_meta *out);
  int32_t (*set_meta)(void *context, const char *path,
                      const struct vfs_perm_meta *meta);
  bool (*is_ready)(void *context);
};
