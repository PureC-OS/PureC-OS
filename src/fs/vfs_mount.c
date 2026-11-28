#include "vfs_mount.h"
#include "../kernel/ns/mnt.h"
#include "../kernel/sync/mutex.h"
#include "../lib/string.h"
#include "../mm/oom/oom_slab.h"

static mutex_t mnt_lock;

static char *dup_text(const char *text) {
  uint32_t length = (uint32_t)strlen(text) + 1;
  char *copy = oom_slab_alloc(length);
  if (copy)
    memcpy(copy, text, length);
  return copy;
}

static void free_text(char *text) {
  if (text)
    oom_slab_free(text, (uint64_t)strlen(text) + 1);
}

static bool equal(const char *left, const char *right) {
  return strcmp(left, right) == 0;
}

struct vfs_super *vfs_super_create(const struct vfs_fs_ops *ops, void *context,
                                   const char *source,
                                   void (*destroy)(void *context)) {
  struct vfs_super *super = oom_slab_alloc(sizeof(*super));
  if (!super)
    return 0;
  memset(super, 0, sizeof(*super));
  super->source = dup_text(source ? source : ops->name);
  if (!super->source) {
    oom_slab_free(super, sizeof(*super));
    return 0;
  }
  super->refcount = 1;
  super->ops = ops;
  super->context = context;
  super->destroy = destroy;
  return super;
}

struct vfs_super *vfs_super_get(struct vfs_super *super) {
  if (super)
    __atomic_fetch_add(&super->refcount, 1, __ATOMIC_RELAXED);
  return super;
}

void vfs_super_put(struct vfs_super *super) {
  if (!super)
    return;
  if (__atomic_sub_fetch(&super->refcount, 1, __ATOMIC_ACQ_REL) != 0)
    return;
  if (super->destroy)
    super->destroy(super->context);
  free_text(super->source);
  oom_slab_free(super, sizeof(*super));
}

struct vfs_mount *vfs_mount_ref(struct vfs_mount *mount) {
  if (mount)
    __atomic_fetch_add(&mount->refcount, 1, __ATOMIC_RELAXED);
  return mount;
}

void vfs_mount_unref(struct vfs_mount *mount) {
  if (!mount)
    return;
  if (__atomic_sub_fetch(&mount->refcount, 1, __ATOMIC_ACQ_REL) != 0)
    return;
  vfs_super_put(mount->super);
  free_text(mount->path);
  free_text(mount->root);
  oom_slab_free(mount, sizeof(*mount));
}

static struct vfs_mount *new_mount(const char *path, const char *root,
                                   struct vfs_super *super, uint32_t flags) {
  struct vfs_mount *mount = oom_slab_alloc(sizeof(*mount));
  if (!mount)
    return 0;
  memset(mount, 0, sizeof(*mount));
  mount->path = dup_text(path);
  mount->root = dup_text(root ? root : "");
  if (!mount->path || !mount->root) {
    free_text(mount->path);
    free_text(mount->root);
    oom_slab_free(mount, sizeof(*mount));
    return 0;
  }
  mount->super = vfs_super_get(super);
  mount->ops = super->ops;
  mount->context = super->context;
  mount->flags = flags;
  mount->refcount = 1;
  return mount;
}

static struct vfs_mount *at_locked(struct mnt_namespace *ns, const char *path) {
  for (struct vfs_mount *mount = ns->mounts; mount; mount = mount->next)
    if (equal(mount->path, path))
      return mount;
  return 0;
}

static void unlink_locked(struct mnt_namespace *ns, struct vfs_mount *mount) {
  for (struct vfs_mount **link = &ns->mounts; *link; link = &(*link)->next) {
    if (*link != mount)
      continue;
    *link = mount->next;
    mount->next = 0;
    mount->detached = true;
    ns->count--;
    vfs_mount_unref(mount);
    return;
  }
}

static void link_locked(struct mnt_namespace *ns, struct vfs_mount *mount) {
  struct vfs_mount *existing = at_locked(ns, mount->path);
  if (existing)
    unlink_locked(ns, existing);
  struct vfs_mount **tail = &ns->mounts;
  while (*tail)
    tail = &(*tail)->next;
  *tail = mount;
  ns->count++;
}

static int32_t add_locked(struct mnt_namespace *ns, const char *path,
                          struct vfs_super *super, const char *root,
                          uint32_t flags) {
  char normalized[VFS_PATH_MAX];
  if (!path || !super ||
      vfs_path_normalize(path, normalized, sizeof(normalized)) < 0)
    return FS_ERROR_INVALID;
  struct vfs_mount *mount = new_mount(normalized, root, super, flags);
  if (!mount)
    return FS_ERROR_NO_SPACE;
  link_locked(ns, mount);
  return 0;
}

int32_t vfs_mount_add_super(struct mnt_namespace *ns, const char *path,
                            struct vfs_super *super, const char *root,
                            uint32_t flags) {
  MUTEX_SCOPE(&mnt_lock);
  return add_locked(ns, path, super, root, flags);
}

int32_t vfs_mount_replace(const char *path, const struct vfs_fs_ops *ops,
                          void *context, const char *source, uint32_t flags) {
  if (!ops)
    return FS_ERROR_INVALID;
  struct vfs_super *super = vfs_super_create(ops, context, source, 0);
  if (!super)
    return FS_ERROR_NO_SPACE;
  int32_t status = vfs_mount_add_super(&init_mnt_ns, path, super, 0, flags);
  vfs_super_put(super);
  return status;
}

static bool under(const char *path, const char *prefix) {
  return vfs_path_has_prefix(path, prefix);
}

int32_t vfs_mount_detach(struct mnt_namespace *ns, const char *path,
                         bool lazy) {
  char normalized[VFS_PATH_MAX];
  if (!path || vfs_path_normalize(path, normalized, sizeof(normalized)) < 0)
    return FS_ERROR_INVALID;
  if (equal(normalized, "/"))
    return FS_ERROR_BUSY;
  MUTEX_SCOPE(&mnt_lock);
  struct vfs_mount *target = at_locked(ns, normalized);
  if (!target)
    return FS_ERROR_NOT_FOUND;
  if (!lazy) {
    if (__atomic_load_n(&target->refcount, __ATOMIC_ACQUIRE) > 1)
      return FS_ERROR_BUSY;
    for (struct vfs_mount *mount = ns->mounts; mount; mount = mount->next)
      if (mount != target && under(mount->path, normalized))
        return FS_ERROR_BUSY;
  }
  struct vfs_mount **link = &ns->mounts;
  while (*link) {
    struct vfs_mount *mount = *link;
    if (mount == target || (lazy && under(mount->path, normalized))) {
      *link = mount->next;
      mount->next = 0;
      mount->detached = true;
      ns->count--;
      vfs_mount_unref(mount);
      continue;
    }
    link = &mount->next;
  }
  return 0;
}

int32_t vfs_mount_remove(const char *path) {
  return vfs_mount_detach(&init_mnt_ns, path, false);
}

int32_t vfs_mount_bind(struct mnt_namespace *ns, struct vfs_mount *source,
                       const char *source_path, const char *source_root,
                       const char *target, bool recursive) {
  char target_path[VFS_PATH_MAX];
  char from[VFS_PATH_MAX];
  if (vfs_path_normalize(target, target_path, sizeof(target_path)) < 0 ||
      vfs_path_normalize(source_path, from, sizeof(from)) < 0)
    return FS_ERROR_INVALID;
  MUTEX_SCOPE(&mnt_lock);
  struct vfs_mount *chain = 0;
  struct vfs_mount **chain_tail = &chain;
  if (recursive) {
    uint32_t from_length = (uint32_t)strlen(from);
    uint32_t target_length = (uint32_t)strlen(target_path);
    uint32_t base = target_length == 1 ? 0 : target_length;
    for (struct vfs_mount *mount = ns->mounts; mount; mount = mount->next) {
      if (mount == source || equal(mount->path, from) ||
          !under(mount->path, from))
        continue;
      const char *suffix =
          from_length == 1 ? mount->path : mount->path + from_length;
      uint32_t length = base + (uint32_t)strlen(suffix) + 1;
      char *joined = oom_slab_alloc(length);
      struct vfs_mount *copy = 0;
      if (joined) {
        memcpy(joined, target_path, base);
        strcpy(joined + base, suffix);
        copy = new_mount(joined, mount->root, mount->super, mount->flags);
        oom_slab_free(joined, length);
      }
      if (!copy) {
        while (chain) {
          struct vfs_mount *drop = chain;
          chain = drop->next;
          vfs_mount_unref(drop);
        }
        return FS_ERROR_NO_SPACE;
      }
      *chain_tail = copy;
      chain_tail = &copy->next;
    }
  }
  int32_t status =
      add_locked(ns, target_path, source->super, source_root, source->flags);
  while (chain) {
    struct vfs_mount *copy = chain;
    chain = copy->next;
    copy->next = 0;
    if (status < 0)
      vfs_mount_unref(copy);
    else
      link_locked(ns, copy);
  }
  return status;
}

struct vfs_mount *vfs_mount_at_in(struct mnt_namespace *ns, const char *path) {
  MUTEX_SCOPE(&mnt_lock);
  return at_locked(ns, path);
}

struct vfs_mount *vfs_mount_at(const char *path) {
  return vfs_mount_at_in(mnt_ns_current(), path);
}

struct vfs_mount *vfs_mount_find(const char *path, const char **relative,
                                 char *relative_buffer, uint32_t capacity,
                                 const char **rest_out, uint32_t *root_length) {
  MUTEX_SCOPE(&mnt_lock);
  struct mnt_namespace *ns = mnt_ns_current();
  struct vfs_mount *best = 0;
  uint32_t best_length = 0;
  for (struct vfs_mount *mount = ns->mounts; mount; mount = mount->next) {
    if (!vfs_path_has_prefix(path, mount->path))
      continue;
    uint32_t length = (uint32_t)strlen(mount->path);
    if (!best || length > best_length) {
      best = mount;
      best_length = length;
    }
  }
  if (!best)
    return 0;
  const char *rest = best_length == 1 ? path : path + best_length;
  uint32_t prefix = (uint32_t)strlen(best->root);
  if (rest_out)
    *rest_out = rest;
  if (root_length)
    *root_length = prefix;
  if (relative) {
    if (prefix) {
      uint32_t tail = (uint32_t)strlen(rest);
      if (tail == 1)
        tail = 0;
      if (!relative_buffer || prefix + tail + 1 > capacity)
        return 0;
      memcpy(relative_buffer, best->root, prefix);
      memcpy(relative_buffer + prefix, rest, tail);
      relative_buffer[prefix + tail] = '\0';
      *relative = relative_buffer;
    } else {
      if (!rest[0]) {
        if (!relative_buffer || capacity < 2)
          return 0;
        relative_buffer[0] = '/';
        relative_buffer[1] = '\0';
        rest = relative_buffer;
      }
      *relative = rest;
    }
  }
  return best;
}

uint32_t vfs_mount_child_count(const char *directory) {
  MUTEX_SCOPE(&mnt_lock);
  uint32_t found = 0;
  char parent[VFS_PATH_MAX];
  for (struct vfs_mount *mount = mnt_ns_current()->mounts; mount;
       mount = mount->next) {
    if (equal(mount->path, "/") ||
        vfs_path_parent(mount->path, parent, sizeof(parent)) < 0)
      continue;
    if (equal(parent, directory))
      found++;
  }
  return found;
}

const char *vfs_mount_child_name(const char *directory, uint32_t index) {
  MUTEX_SCOPE(&mnt_lock);
  char parent[VFS_PATH_MAX];
  for (struct vfs_mount *mount = mnt_ns_current()->mounts; mount;
       mount = mount->next) {
    if (equal(mount->path, "/") ||
        vfs_path_parent(mount->path, parent, sizeof(parent)) < 0)
      continue;
    if (!equal(parent, directory))
      continue;
    if (index == 0)
      return vfs_path_basename(mount->path);
    index--;
  }
  return 0;
}

void vfs_mount_foreach(struct mnt_namespace *ns, vfs_mount_visit_fn visit,
                       void *arg) {
  MUTEX_SCOPE(&mnt_lock);
  for (struct vfs_mount *mount = ns->mounts; mount; mount = mount->next)
    visit(mount, arg);
}

int32_t vfs_mount_ns_clone(struct mnt_namespace *dst,
                           struct mnt_namespace *src) {
  MUTEX_SCOPE(&mnt_lock);
  struct vfs_mount **tail = &dst->mounts;
  for (struct vfs_mount *mount = src->mounts; mount; mount = mount->next) {
    struct vfs_mount *copy =
        new_mount(mount->path, mount->root, mount->super, mount->flags);
    if (!copy) {
      while (dst->mounts) {
        struct vfs_mount *drop = dst->mounts;
        dst->mounts = drop->next;
        vfs_mount_unref(drop);
      }
      dst->count = 0;
      return FS_ERROR_NO_SPACE;
    }
    *tail = copy;
    tail = &copy->next;
    dst->count++;
  }
  return 0;
}

void vfs_mount_ns_release(struct mnt_namespace *ns) {
  MUTEX_SCOPE(&mnt_lock);
  while (ns->mounts) {
    struct vfs_mount *mount = ns->mounts;
    ns->mounts = mount->next;
    mount->next = 0;
    mount->detached = true;
    vfs_mount_unref(mount);
  }
  ns->count = 0;
}
