#pragma once

#include "vfs_ops.h"
#include "vfs_path.h"
#include <stdbool.h>
#include <stdint.h>

#define VFS_MOUNT_READ_ONLY 1U

struct mnt_namespace;

struct vfs_super {
  uint32_t refcount;
  const struct vfs_fs_ops *ops;
  void *context;
  char *source;
  void (*destroy)(void *context);
};

struct vfs_mount {
  struct vfs_mount *next;
  char *path;
  char *root;
  struct vfs_super *super;
  const struct vfs_fs_ops *ops;
  void *context;
  uint32_t flags;
  uint32_t refcount;
  bool detached;
};

struct vfs_super *vfs_super_create(const struct vfs_fs_ops *ops, void *context,
                                   const char *source,
                                   void (*destroy)(void *context));
struct vfs_super *vfs_super_get(struct vfs_super *super);
void vfs_super_put(struct vfs_super *super);

int32_t vfs_mount_add_super(struct mnt_namespace *ns, const char *path,
                            struct vfs_super *super, const char *root,
                            uint32_t flags);
int32_t vfs_mount_replace(const char *path, const struct vfs_fs_ops *ops,
                          void *context, const char *source, uint32_t flags);
int32_t vfs_mount_remove(const char *path);
int32_t vfs_mount_bind(struct mnt_namespace *ns, struct vfs_mount *source,
                       const char *source_path, const char *source_root,
                       const char *target, bool recursive);
int32_t vfs_mount_detach(struct mnt_namespace *ns, const char *path,
                         bool lazy);

struct vfs_mount *vfs_mount_find(const char *path, const char **relative,
                                 char *relative_buffer, uint32_t capacity,
                                 const char **rest, uint32_t *root_length);
struct vfs_mount *vfs_mount_at(const char *path);
struct vfs_mount *vfs_mount_at_in(struct mnt_namespace *ns, const char *path);
uint32_t vfs_mount_child_count(const char *directory);
const char *vfs_mount_child_name(const char *directory, uint32_t index);

struct vfs_mount *vfs_mount_ref(struct vfs_mount *mount);
void vfs_mount_unref(struct vfs_mount *mount);

typedef void (*vfs_mount_visit_fn)(const struct vfs_mount *mount, void *arg);
void vfs_mount_foreach(struct mnt_namespace *ns, vfs_mount_visit_fn visit,
                       void *arg);

int32_t vfs_mount_ns_clone(struct mnt_namespace *dst,
                           struct mnt_namespace *src);
void vfs_mount_ns_release(struct mnt_namespace *ns);
