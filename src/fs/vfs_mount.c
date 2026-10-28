#include "vfs_mount.h"

static struct vfs_mount mounts[VFS_MOUNT_MAX];

static uint32_t length_of(const char *text) {
  uint32_t length = 0;
  while (text[length])
    length++;
  return length;
}

static bool equal(const char *left, const char *right) {
  while (*left && *left == *right) {
    left++;
    right++;
  }
  return *left == *right;
}

static void copy_text(char *destination, const char *source,
                      uint32_t capacity) {
  uint32_t i = 0;
  while (source && source[i] && i + 1 < capacity) {
    destination[i] = source[i];
    i++;
  }
  destination[i] = '\0';
}

int32_t vfs_mount_add(const char *path, const struct vfs_fs_ops *ops,
                      void *context, const char *source, uint32_t flags) {
  char normalized[VFS_PATH_MAX];
  if (!path || !ops || vfs_path_normalize(path, normalized, sizeof(normalized)) < 0)
    return FS_ERROR_INVALID;
  struct vfs_mount *slot = 0;
  for (uint32_t i = 0; i < VFS_MOUNT_MAX; i++) {
    if (!mounts[i].used) {
      if (!slot)
        slot = &mounts[i];
      continue;
    }
    if (equal(mounts[i].path, normalized))
      return FS_ERROR_BUSY;
  }
  if (!slot)
    return FS_ERROR_NO_SPACE;
  copy_text(slot->path, normalized, sizeof(slot->path));
  copy_text(slot->source, source ? source : ops->name, sizeof(slot->source));
  slot->ops = ops;
  slot->context = context;
  slot->flags = flags;
  slot->used = true;
  return 0;
}

int32_t vfs_mount_replace(const char *path, const struct vfs_fs_ops *ops,
                          void *context, const char *source, uint32_t flags) {
  char normalized[VFS_PATH_MAX];
  if (!path || !ops || vfs_path_normalize(path, normalized, sizeof(normalized)) < 0)
    return FS_ERROR_INVALID;
  struct vfs_mount *existing = vfs_mount_at(normalized);
  if (!existing)
    return vfs_mount_add(normalized, ops, context, source, flags);
  copy_text(existing->source, source ? source : ops->name,
            sizeof(existing->source));
  existing->ops = ops;
  existing->context = context;
  existing->flags = flags;
  return 0;
}

int32_t vfs_mount_remove(const char *path) {
  char normalized[VFS_PATH_MAX];
  if (!path || vfs_path_normalize(path, normalized, sizeof(normalized)) < 0)
    return FS_ERROR_INVALID;
  for (uint32_t i = 0; i < VFS_MOUNT_MAX; i++) {
    if (!mounts[i].used || !equal(mounts[i].path, normalized))
      continue;
    for (uint32_t j = 0; j < VFS_MOUNT_MAX; j++) {
      if (j == i || !mounts[j].used)
        continue;
      if (!equal(mounts[j].path, normalized) &&
          vfs_path_has_prefix(mounts[j].path, normalized))
        return FS_ERROR_BUSY;
    }
    mounts[i].used = false;
    return 0;
  }
  return FS_ERROR_NOT_FOUND;
}

struct vfs_mount *vfs_mount_find(const char *path, const char **relative,
                                 char *relative_buffer, uint32_t capacity) {
  struct vfs_mount *best = 0;
  uint32_t best_length = 0;
  for (uint32_t i = 0; i < VFS_MOUNT_MAX; i++) {
    if (!mounts[i].used || !vfs_path_has_prefix(path, mounts[i].path))
      continue;
    uint32_t length = length_of(mounts[i].path);
    if (!best || length > best_length) {
      best = &mounts[i];
      best_length = length;
    }
  }
  if (!best)
    return 0;
  if (relative) {
    const char *rest = best_length == 1 ? path : path + best_length;
    if (!rest[0]) {
      if (!relative_buffer || capacity < 2)
        return 0;
      relative_buffer[0] = '/';
      relative_buffer[1] = '\0';
      rest = relative_buffer;
    }
    *relative = rest;
  }
  return best;
}

struct vfs_mount *vfs_mount_at(const char *path) {
  for (uint32_t i = 0; i < VFS_MOUNT_MAX; i++)
    if (mounts[i].used && equal(mounts[i].path, path))
      return &mounts[i];
  return 0;
}

uint32_t vfs_mount_count(void) {
  uint32_t count = 0;
  for (uint32_t i = 0; i < VFS_MOUNT_MAX; i++)
    if (mounts[i].used)
      count++;
  return count;
}

const struct vfs_mount *vfs_mount_get(uint32_t index) {
  for (uint32_t i = 0; i < VFS_MOUNT_MAX; i++) {
    if (!mounts[i].used)
      continue;
    if (index == 0)
      return &mounts[i];
    index--;
  }
  return 0;
}

uint32_t vfs_mount_children(const char *directory, const char **names,
                            uint32_t capacity) {
  uint32_t found = 0;
  char parent[VFS_PATH_MAX];
  for (uint32_t i = 0; i < VFS_MOUNT_MAX && found < capacity; i++) {
    if (!mounts[i].used || equal(mounts[i].path, "/"))
      continue;
    if (vfs_path_parent(mounts[i].path, parent, sizeof(parent)) < 0)
      continue;
    if (equal(parent, directory))
      names[found++] = vfs_path_basename(mounts[i].path);
  }
  return found;
}

void vfs_mount_reset(void) {
  for (uint32_t i = 0; i < VFS_MOUNT_MAX; i++)
    mounts[i].used = false;
}
