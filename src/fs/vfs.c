#include "../kernel/sync/mutex.h"
static mutex_t vfs_mutex;
#include "../drivers/storage/block_device.h"
#include "../kernel/diagnostics/klog.h"
#include "../kernel/process/process.h"
#include "../kernel/syscall/syscall.h"
#include "../lib/string.h"
#include "../mm/pmm.h"
#include "./ext2/include/ext2.h"
#include "./ext2/include/ext2_block.h"
#include "./ext2/include/ext2_debug.h"
#include "./ext2/include/ext2_dir.h"
#include "./ext2/include/ext2_inode.h"
#include "./ext2/include/ext2_types.h"
#include "initramfs.h"
#include "vfs.h"
#include "vfs_alias.h"
#include "vfs_backends.h"
#include "vfs_mount.h"
#include "vfs_path.h"
#include "vfs_perm.h"

#define VFS_HANDLE_INITIAL 32
#define VFS_KLOG_WINDOW (8 * 1024 * 1024)

enum vfs_handle_type {
  VFS_HANDLE_NONE,
  VFS_HANDLE_BACKEND,
  VFS_HANDLE_MEMORY,
  VFS_HANDLE_KLOG,
  VFS_HANDLE_APPEND
};

static uint8_t vfs_active_fs = VFS_FS_EXT2;
static bool mounts_ready;

struct vfs_handle {
  enum vfs_handle_type type;
  int32_t backend_descriptor;
  struct vfs_mount *mount;
  const char *data;
  uint32_t size;
  uint32_t position;
  uint64_t klog_cursor;
  bool klog_data_lost;
  uint32_t refs;
  char path[VFS_PATH_MAX];
};

struct vfs_resolved {
  struct vfs_mount *mount;
  const char *relative;
  char path[VFS_PATH_MAX];
  char root[2];
};

static struct vfs_handle *handles;

static bool path_equals(const char *left, const char *right) {
  return left && right && strcmp(left, right) == 0;
}

static void copy_name(char destination[FS_DIRECTORY_NAME_CAPACITY],
                      const char *source) {
  uint32_t i = 0;
  while (source[i] && i + 1 < FS_DIRECTORY_NAME_CAPACITY) {
    destination[i] = source[i];
    i++;
  }
  destination[i] = '\0';
}

static void copy_name_long(char destination[FS_LONG_NAME_CAPACITY],
                           const char *source) {
  uint32_t i = 0;
  while (source[i] && i + 1 < FS_LONG_NAME_CAPACITY) {
    destination[i] = source[i];
    i++;
  }
  destination[i] = '\0';
}

static void log_mount(const char *path);

static bool live_mode;
static bool tmpfs_synced;

static void mount_tmpfs(const char *path, const char *const *seed) {
  if (vfs_mount_at(path))
    return;
  void *instance = vfs_tmpfs_create();
  if (!instance)
    return;
  if (vfs_mount_add(path, &vfs_tmpfs_ops, instance, "tmpfs", 0) < 0)
    return;
  for (uint32_t i = 0; seed && seed[i]; i++)
    (void)vfs_tmpfs_ops.create_directory(instance, seed[i]);
  log_mount(path);
  if (path_equals(path, "/tmp") || path_equals(path, "/run")) {
    struct vfs_perm_meta shared = {01777, 0, 0};
    (void)vfs_perm_set(path, &shared);
  }
}

static void unmount_live_tmpfs(const char *path) {
  const struct vfs_mount *mount = vfs_mount_at(path);
  if (mount && mount->ops == &vfs_tmpfs_ops)
    (void)vfs_mount_remove(path);
}

static void sync_tmpfs_mounts(void) {
  if (tmpfs_synced || !pmm_is_ready())
    return;
  tmpfs_synced = true;
  mount_tmpfs("/tmp", 0);
  mount_tmpfs("/run", 0);
  if (live_mode) {
    static const char *const var_seed[] = {"/log", "/tmp", "/cache", 0};
    mount_tmpfs("/var", var_seed);
    mount_tmpfs("/home", 0);
    mount_tmpfs("/root", 0);
  } else {
    unmount_live_tmpfs("/var");
    unmount_live_tmpfs("/home");
    unmount_live_tmpfs("/root");
  }
}

static void ensure_mounts(void) {
  if (mounts_ready) {
    sync_tmpfs_mounts();
    return;
  }
  mounts_ready = true;
  (void)vfs_mount_replace("/", &vfs_ext2_ops, 0, "ext2", 0);
  (void)vfs_mount_replace("/kernel", &vfs_kernelfs_ops, 0, "kernelfs",
                          VFS_MOUNT_READ_ONLY);
  (void)vfs_mount_replace("/dev", &vfs_devfs_ops, 0, "devfs", 0);
  (void)vfs_mount_replace("/proc", &vfs_procfs_ops, 0, "proc",
                          VFS_MOUNT_READ_ONLY);
  (void)vfs_mount_replace("/sys", &vfs_sysfs_ops, 0, "sysfs",
                          VFS_MOUNT_READ_ONLY);
  log_mount("/kernel");
  log_mount("/dev");
  log_mount("/proc");
  log_mount("/sys");
  sync_tmpfs_mounts();
}

static void log_mount(const char *path) {
  const struct vfs_mount *mount = vfs_mount_at(path);
  if (mount)
    klogf(KLOG_INFO, "vfs: %s on %s type %s%s", mount->source, mount->path,
          mount->ops->name,
          (mount->flags & VFS_MOUNT_READ_ONLY) ? " (ro)" : "");
}

static void set_root_ops(uint8_t fs_type) {
  ensure_mounts();
  vfs_active_fs = fs_type;
  live_mode = fs_type == VFS_FS_INITRAMFS;
  tmpfs_synced = false;
  if (fs_type == VFS_FS_INITRAMFS)
    (void)vfs_mount_replace("/", &vfs_initramfs_ops, 0, "initramfs",
                            VFS_MOUNT_READ_ONLY);
  else
    (void)vfs_mount_replace("/", &vfs_ext2_ops, 0,
                            ext2_is_mounted() ? ext2_device_name() : "ext2", 0);
  log_mount("/");
  sync_tmpfs_mounts();
}

static int32_t resolve(const char *input, struct vfs_resolved *resolved) {
  ensure_mounts();
  if (!input || !input[0])
    return FS_ERROR_INVALID;
  char absolute[VFS_PATH_MAX];
  if (input[0] != '/') {
    if (strlen(input) + 2 > sizeof(absolute))
      return FS_ERROR_INVALID;
    absolute[0] = '/';
    strcpy(absolute + 1, input);
    input = absolute;
  }
  if (vfs_alias_resolve(input, resolved->path, sizeof(resolved->path)) < 0)
    return FS_ERROR_INVALID;
  resolved->mount = vfs_mount_find(resolved->path, &resolved->relative,
                                   resolved->root, sizeof(resolved->root));
  return resolved->mount ? 0 : FS_ERROR_NOT_FOUND;
}

static const char *const virtual_dirs[] = {"/mnt", "/opt", "/srv", "/usr/local",
                                           "/usr/local/bin", "/lost+found"};

#define VIRTUAL_DIR_COUNT (sizeof(virtual_dirs) / sizeof(virtual_dirs[0]))

static bool is_virtual_dir(const char *path) {
  if (!live_mode)
    return false;
  for (uint32_t i = 0; i < VIRTUAL_DIR_COUNT; i++)
    if (strcmp(path, virtual_dirs[i]) == 0)
      return true;
  return false;
}

static uint32_t virtual_children(const char *directory, const char **names,
                                 uint32_t capacity) {
  uint32_t found = 0;
  if (!live_mode)
    return 0;
  char parent[VFS_PATH_MAX];
  for (uint32_t i = 0; i < VIRTUAL_DIR_COUNT && found < capacity; i++) {
    if (vfs_path_parent(virtual_dirs[i], parent, sizeof(parent)) < 0)
      continue;
    if (strcmp(parent, directory) == 0)
      names[found++] = vfs_path_basename(virtual_dirs[i]);
  }
  return found;
}

static bool mount_ready(const struct vfs_mount *mount);

static void parent_of(const char *path, char *out, uint32_t capacity) {
  uint32_t length = (uint32_t)strlen(path);
  if (length >= capacity)
    length = capacity - 1;
  memcpy(out, path, length);
  out[length] = '\0';
  while (length > 1 && out[length - 1] == '/')
    out[--length] = '\0';
  while (length > 1 && out[length - 1] != '/')
    length--;
  if (length > 1)
    length--;
  out[length] = '\0';
  if (!out[0]) {
    out[0] = '/';
    out[1] = '\0';
  }
}

static struct vfs_mount *meta_mount(const char *path, const char **relative,
                                    char *buffer, uint32_t capacity) {
  struct vfs_mount *mount = vfs_mount_find(path, relative, buffer, capacity);
  if (!mount || !mount->ops->get_meta || !mount->ops->set_meta ||
      !mount_ready(mount))
    return 0;
  return mount;
}

static void effective_meta(const char *path, bool directory,
                           struct vfs_perm_meta *meta) {
  const char *relative;
  char buffer[2];
  struct vfs_mount *mount = meta_mount(path, &relative, buffer, sizeof(buffer));
  if (mount && mount->ops->get_meta(mount->context, relative, meta) >= 0)
    return;
  if (!vfs_perm_get(path, meta))
    vfs_perm_default(meta, directory);
}

static int32_t store_meta(const char *path, const struct vfs_perm_meta *meta) {
  const char *relative;
  char buffer[2];
  struct vfs_mount *mount = meta_mount(path, &relative, buffer, sizeof(buffer));
  if (mount && !(mount->flags & VFS_MOUNT_READ_ONLY))
    return mount->ops->set_meta(mount->context, relative, meta);
  return vfs_perm_set(path, meta);
}

static int32_t check_access(const char *path, bool directory, uint32_t want) {
  uint32_t uid = process_current_uid();
  if (uid == 0)
    return 0;
  struct vfs_perm_meta meta;
  effective_meta(path, directory, &meta);
  return vfs_perm_allowed(uid, process_current_gid(), &meta, want)
             ? 0
             : FS_ERROR_DENIED;
}

static int32_t check_parent_write(const char *path) {
  char parent[VFS_PATH_MAX];
  parent_of(path, parent, sizeof(parent));
  return check_access(parent, true, VFS_PERM_W);
}

static bool path_exists(const struct vfs_resolved *resolved) {
  struct file_stat_info info;
  const struct vfs_fs_ops *ops = resolved->mount->ops;
  return ops->stat && mount_ready(resolved->mount) &&
         ops->stat(resolved->mount->context, resolved->relative, &info) >= 0;
}

static void own_created(const char *path, bool directory) {
  uint32_t uid = process_current_uid();
  if (uid == 0)
    return;
  struct vfs_perm_meta meta;
  vfs_perm_default(&meta, directory);
  meta.uid = uid;
  meta.gid = process_current_gid();
  (void)store_meta(path, &meta);
}

static int32_t check_write_target(const struct vfs_resolved *resolved,
                                  bool *created) {
  *created = false;
  if (process_current_uid() == 0)
    return 0;
  if (path_exists(resolved))
    return check_access(resolved->path, false, VFS_PERM_W);
  *created = true;
  return check_parent_write(resolved->path);
}

static bool mount_ready(const struct vfs_mount *mount) {
  return !mount->ops->is_ready || mount->ops->is_ready(mount->context);
}

static int32_t check_mutation(const struct vfs_mount *mount, const void *op) {
  if (mount->flags & VFS_MOUNT_READ_ONLY)
    return FS_ERROR_READ_ONLY;
  if (!op || !mount_ready(mount))
    return FS_ERROR_UNSUPPORTED;
  return 0;
}

static bool is_klog_path(const char *path) {
  return path &&
         (strcmp(path, "/kernel.log") == 0 || strcmp(path, "/dmesg.txt") == 0);
}

static bool overlay_find(const char *path, const void **data, uint32_t *size) {
  return initramfs_is_mounted() && initramfs_find(path, data, size);
}

static uint32_t klog_visible_size(void) {
  uint64_t total = klog_total_bytes();
  return total > VFS_KLOG_WINDOW ? VFS_KLOG_WINDOW : (uint32_t)total;
}

static uint32_t handles_capacity;
static uint64_t handles_phys;
static uint64_t handles_pages;

static bool handles_ensure(uint32_t want) {
  if (want < handles_capacity)
    return true;
  uint32_t grown = handles_capacity ? handles_capacity * 2 : VFS_HANDLE_INITIAL;
  if (grown < want)
    grown = want;
  if (grown > (1u << 20))
    return false;
  uint64_t pages = ((uint64_t)grown * sizeof(struct vfs_handle) + 4095) / 4096;
  uint64_t phys = pmm_allocate_contiguous(pages);
  if (!phys)
    return false;
  struct vfs_handle *tab = (struct vfs_handle *)pmm_physical_to_virtual(phys);
  memset(tab, 0, (size_t)(pages * 4096));
  if (handles && handles_capacity)
    memcpy(tab, handles, (size_t)handles_capacity * sizeof(struct vfs_handle));
  if (handles_phys)
    pmm_free_contiguous(handles_phys, handles_pages);
  handles = tab;
  handles_capacity = (uint32_t)((pages * 4096) / sizeof(struct vfs_handle));
  handles_phys = phys;
  handles_pages = pages;
  return true;
}

static int32_t allocate_handle(void) {
  for (uint32_t i = 0; i < handles_capacity; i++)
    if (handles[i].type == VFS_HANDLE_NONE)
      return (int32_t)i;
  uint32_t at = handles_capacity;
  if (!handles_ensure(at + 1))
    return FS_ERROR_NO_SPACE;
  return (int32_t)at;
}

static struct vfs_handle *get_handle(int32_t descriptor) {
  int32_t idx = descriptor - VFS_FD_BASE;
  if (idx < 0 || (uint32_t)idx >= handles_capacity)
    return 0;
  if (!handles || handles[idx].type == VFS_HANDLE_NONE)
    return 0;
  return &handles[idx];
}

const char *vfs_fs_type_name(uint8_t fs_type) {
  MUTEX_SCOPE(&vfs_mutex);
  if (fs_type == VFS_FS_EXT2)
    return "ext2";
  if (fs_type == VFS_FS_INITRAMFS)
    return "initramfs";
  return "unknown";
}

uint8_t vfs_root_fs_type(void) {
  MUTEX_SCOPE(&vfs_mutex);
  return vfs_active_fs;
}

void vfs_set_active_fs(uint8_t fs_type) {
  MUTEX_SCOPE(&vfs_mutex);
  set_root_ops(fs_type);
}

bool vfs_mount_root(void) {
  MUTEX_SCOPE(&vfs_mutex);
  return vfs_mount_root_with_fs(VFS_FS_AUTO);
}

bool vfs_mount_initramfs(void) {
  MUTEX_SCOPE(&vfs_mutex);
  if (!initramfs_mount())
    return false;
  set_root_ops(VFS_FS_INITRAMFS);
  return true;
}

bool vfs_mount_root_with_fs(uint8_t fs_type) {
  MUTEX_SCOPE(&vfs_mutex);
  if (fs_type != VFS_FS_EXT2 && fs_type != VFS_FS_AUTO)
    return false;
  if (ext2_init()) {
    set_root_ops(VFS_FS_EXT2);
    return true;
  }
  return false;
}

bool vfs_is_root_mounted(void) {
  MUTEX_SCOPE(&vfs_mutex);
  if (vfs_active_fs == VFS_FS_INITRAMFS)
    return initramfs_is_mounted();
  return ext2_is_mounted();
}

const char *vfs_root_device_name(void) {
  MUTEX_SCOPE(&vfs_mutex);
  if (vfs_active_fs == VFS_FS_INITRAMFS)
    return "initramfs";
  return ext2_device_name();
}

int32_t vfs_open(const char *path) {
  MUTEX_SCOPE(&vfs_mutex);
  struct vfs_resolved resolved;
  int32_t status = resolve(path, &resolved);
  if (status < 0)
    return status;
  status = check_access(resolved.path, false, VFS_PERM_R);
  if (status < 0)
    return status;
  int32_t idx = allocate_handle();
  if (idx < 0)
    return idx;
  struct vfs_handle *h = &handles[idx];
  memset(h, 0, sizeof(*h));
  h->refs = 1;
  h->backend_descriptor = -1;
  strncpy(h->path, resolved.path, sizeof(h->path) - 1);
  if (is_klog_path(resolved.path)) {
    h->type = VFS_HANDLE_KLOG;
    uint64_t total = klog_total_bytes();
    h->klog_cursor = total > VFS_KLOG_WINDOW ? total - VFS_KLOG_WINDOW : 0;
    return VFS_FD_BASE + idx;
  }
  const void *data = 0;
  uint32_t size = 0;
  if (overlay_find(resolved.path, &data, &size)) {
    h->type = VFS_HANDLE_MEMORY;
    h->data = data;
    h->size = size;
    return VFS_FD_BASE + idx;
  }
  const struct vfs_fs_ops *ops = resolved.mount->ops;
  if (ops->map && ops->map(resolved.mount->context, resolved.relative, &data,
                           &size) == 0) {
    h->type = VFS_HANDLE_MEMORY;
    h->data = data;
    h->size = size;
    return VFS_FD_BASE + idx;
  }
  int32_t backend = FS_ERROR_NOT_FOUND;
  if (ops->open && mount_ready(resolved.mount))
    backend = ops->open(resolved.mount->context, resolved.relative);
  if (backend < 0) {
    memset(h, 0, sizeof(*h));
    return backend;
  }
  h->type = VFS_HANDLE_BACKEND;
  h->backend_descriptor = backend;
  h->mount = resolved.mount;
  return VFS_FD_BASE + idx;
}

int32_t vfs_read(int32_t descriptor, void *buffer, uint32_t count) {
  MUTEX_SCOPE(&vfs_mutex);
  if (!buffer && count)
    return FS_ERROR_INVALID;
  struct vfs_handle *h = get_handle(descriptor);
  if (!h)
    return FS_ERROR_INVALID;
  if (h->type == VFS_HANDLE_APPEND)
    return FS_ERROR_UNSUPPORTED;
  if (h->type == VFS_HANDLE_BACKEND) {
    if (!h->mount->ops->read)
      return FS_ERROR_UNSUPPORTED;
    return h->mount->ops->read(h->mount->context, h->backend_descriptor,
                               buffer, count);
  }
  if (h->type == VFS_HANDLE_KLOG) {
    uint32_t a = klog_read_since(&h->klog_cursor, (char *)buffer, count,
                                 &h->klog_data_lost);
    return (int32_t)a;
  }
  if (h->position >= h->size)
    return 0;
  uint32_t rem = h->size - h->position;
  uint32_t amt = count < rem ? count : rem;
  if (amt)
    memcpy(buffer, h->data + h->position, amt);
  h->position += amt;
  return (int32_t)amt;
}

int32_t vfs_open_flags(const char *path, uint32_t flags) {
  uint32_t access = flags & 3U;
  bool create = (flags & 0100U) != 0;
  bool exclusive = (flags & 0200U) != 0;
  bool truncate = (flags & 01000U) != 0;
  if (access == 0 && !truncate)
    return vfs_open(path);
  struct file_stat_info info;
  bool exists = vfs_stat(path, &info) >= 0;
  if (exists && create && exclusive)
    return FS_ERROR_EXISTS;
  if (exists && info.is_directory)
    return FS_ERROR_INVALID;
  if (!exists) {
    if (!create)
      return FS_ERROR_NOT_FOUND;
    int32_t created = vfs_create_file(path);
    if (created < 0)
      return created;
  } else if (truncate) {
    int32_t cleared = vfs_write_file(path, "", 0);
    if (cleared < 0)
      return cleared;
  }
  if (access == 0)
    return vfs_open(path);
  MUTEX_SCOPE(&vfs_mutex);
  struct vfs_resolved resolved;
  int32_t status = resolve(path, &resolved);
  if (status < 0)
    return status;
  status = check_access(resolved.path, false, VFS_PERM_W);
  if (status < 0)
    return status;
  int32_t idx = allocate_handle();
  if (idx < 0)
    return idx;
  struct vfs_handle *h = &handles[idx];
  memset(h, 0, sizeof(*h));
  h->refs = 1;
  h->backend_descriptor = -1;
  h->type = VFS_HANDLE_APPEND;
  strncpy(h->path, resolved.path, sizeof(h->path) - 1);
  return VFS_FD_BASE + idx;
}

int32_t vfs_write(int32_t descriptor, const void *buffer, uint32_t count) {
  char path[VFS_PATH_MAX];
  {
    MUTEX_SCOPE(&vfs_mutex);
    struct vfs_handle *h = get_handle(descriptor);
    if (!h)
      return FS_ERROR_INVALID;
    if (h->type != VFS_HANDLE_APPEND)
      return FS_ERROR_UNSUPPORTED;
    strncpy(path, h->path, sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';
  }
  if (!count)
    return 0;
  return vfs_append_file(path, buffer, count);
}

int32_t vfs_handle_stat(int32_t descriptor, struct file_stat_info *out) {
  char path[VFS_PATH_MAX];
  {
    MUTEX_SCOPE(&vfs_mutex);
    struct vfs_handle *h = get_handle(descriptor);
    if (!h || !h->path[0])
      return FS_ERROR_INVALID;
    strncpy(path, h->path, sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';
  }
  return vfs_stat(path, out);
}

int32_t vfs_handle_path(int32_t descriptor, char *out, uint32_t capacity) {
  MUTEX_SCOPE(&vfs_mutex);
  struct vfs_handle *h = get_handle(descriptor);
  if (!h || !h->path[0] || !out || !capacity)
    return FS_ERROR_INVALID;
  size_t length = strlen(h->path);
  if (length + 1 > capacity)
    return FS_ERROR_INVALID;
  memcpy(out, h->path, length + 1);
  return (int32_t)length;
}

int32_t vfs_ref(int32_t descriptor) {
  MUTEX_SCOPE(&vfs_mutex);
  struct vfs_handle *h = get_handle(descriptor);
  if (!h || h->refs == UINT32_MAX)
    return FS_ERROR_INVALID;
  h->refs++;
  return 0;
}

int32_t vfs_close(int32_t descriptor) {
  MUTEX_SCOPE(&vfs_mutex);
  struct vfs_handle *h = get_handle(descriptor);
  if (!h)
    return FS_ERROR_INVALID;
  if (h->refs > 1) {
    h->refs--;
    return 0;
  }
  int32_t r = 0;
  if (h->type == VFS_HANDLE_BACKEND && h->mount->ops->close)
    r = h->mount->ops->close(h->mount->context, h->backend_descriptor);
  memset(h, 0, sizeof(*h));
  return r;
}

int64_t vfs_seek(int32_t descriptor, int64_t offset, uint32_t whence) {
  MUTEX_SCOPE(&vfs_mutex);
  if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END)
    return FS_ERROR_INVALID;
  struct vfs_handle *h = get_handle(descriptor);
  if (!h)
    return FS_ERROR_INVALID;
  if (h->type == VFS_HANDLE_APPEND)
    return FS_ERROR_UNSUPPORTED;
  if (h->type == VFS_HANDLE_BACKEND) {
    if (!h->mount->ops->seek)
      return FS_ERROR_UNSUPPORTED;
    return h->mount->ops->seek(h->mount->context, h->backend_descriptor,
                               offset, whence);
  }
  if (h->type == VFS_HANDLE_KLOG) {
    uint64_t total = klog_total_bytes();
    int64_t base = 0;
    if (whence == SEEK_CUR)
      base = (int64_t)h->klog_cursor;
    else if (whence == SEEK_END)
      base = (int64_t)total;
    int64_t target = base + offset;
    if (target < 0)
      target = 0;
    if ((uint64_t)target > total)
      target = (int64_t)total;
    h->klog_cursor = (uint64_t)target;
    return target;
  }
  int64_t base = 0;
  if (whence == SEEK_CUR)
    base = (int64_t)h->position;
  else if (whence == SEEK_END)
    base = (int64_t)h->size;
  int64_t target = base + offset;
  if (target < 0)
    return FS_ERROR_INVALID;
  h->position = (uint32_t)(target > 0xFFFFFFFFLL ? 0xFFFFFFFFLL : target);
  return (int64_t)h->position;
}

int32_t vfs_stat(const char *path, struct file_stat_info *out) {
  MUTEX_SCOPE(&vfs_mutex);
  if (!out)
    return FS_ERROR_INVALID;
  struct vfs_resolved resolved;
  int32_t status = resolve(path, &resolved);
  if (status < 0)
    return status;
  out->reserved = 0;
  if (is_klog_path(resolved.path)) {
    out->size = klog_total_bytes();
    out->is_directory = 0;
    return 0;
  }
  const void *data = 0;
  uint32_t size = 0;
  if (overlay_find(resolved.path, &data, &size)) {
    out->size = size;
    out->is_directory = 0;
    return 0;
  }
  const struct vfs_fs_ops *ops = resolved.mount->ops;
  int32_t result = FS_ERROR_NOT_FOUND;
  if (ops->stat && mount_ready(resolved.mount))
    result = ops->stat(resolved.mount->context, resolved.relative, out);
  if (result < 0 && is_virtual_dir(resolved.path)) {
    out->size = 0;
    out->is_directory = 1;
    out->reserved = FS_NODE_FILE;
    return 0;
  }
  return result;
}

int32_t vfs_delete(const char *path) {
  MUTEX_SCOPE(&vfs_mutex);
  struct vfs_resolved resolved;
  int32_t status = resolve(path, &resolved);
  if (status < 0)
    return status;
  if (vfs_mount_at(resolved.path))
    return FS_ERROR_BUSY;
  status = check_mutation(resolved.mount, resolved.mount->ops->unlink);
  if (status < 0)
    return status;
  status = check_parent_write(resolved.path);
  if (status < 0)
    return status;
  status = resolved.mount->ops->unlink(resolved.mount->context,
                                       resolved.relative);
  if (status >= 0)
    vfs_perm_forget(resolved.path);
  return status;
}

int32_t vfs_rename(const char *path, const char *new_name) {
  MUTEX_SCOPE(&vfs_mutex);
  struct vfs_resolved resolved;
  int32_t status = resolve(path, &resolved);
  if (status < 0)
    return status;
  if (vfs_mount_at(resolved.path))
    return FS_ERROR_BUSY;
  status = check_mutation(resolved.mount, resolved.mount->ops->rename);
  if (status < 0)
    return status;
  status = check_parent_write(resolved.path);
  if (status < 0)
    return status;
  status = resolved.mount->ops->rename(resolved.mount->context,
                                       resolved.relative, new_name);
  if (status >= 0) {
    char parent[VFS_PATH_MAX];
    char renamed[VFS_PATH_MAX];
    parent_of(resolved.path, parent, sizeof(parent));
    uint32_t length = (uint32_t)strlen(parent);
    if (length + strlen(new_name) + 2 <= sizeof(renamed)) {
      memcpy(renamed, parent, length);
      if (length > 1)
        renamed[length++] = '/';
      strcpy(renamed + length, new_name);
      vfs_perm_rename(resolved.path, renamed);
    }
  }
  return status;
}

int32_t vfs_move(const char *path, const char *dest) {
  MUTEX_SCOPE(&vfs_mutex);
  struct vfs_resolved source;
  struct vfs_resolved target;
  int32_t status = resolve(path, &source);
  if (status < 0)
    return status;
  status = resolve(dest, &target);
  if (status < 0)
    return status;
  if (vfs_mount_at(source.path))
    return FS_ERROR_BUSY;
  if (source.mount != target.mount)
    return FS_ERROR_UNSUPPORTED;
  status = check_mutation(source.mount, source.mount->ops->move);
  if (status < 0)
    return status;
  status = check_parent_write(source.path);
  if (status < 0)
    return status;
  status = check_access(target.path, true, VFS_PERM_W);
  if (status < 0)
    return status;
  status = source.mount->ops->move(source.mount->context, source.relative,
                                   target.relative);
  if (status >= 0) {
    const char *base = source.path;
    for (const char *c = source.path; *c; c++)
      if (*c == '/')
        base = c + 1;
    char moved[VFS_PATH_MAX];
    uint32_t length = (uint32_t)strlen(target.path);
    if (length + strlen(base) + 2 <= sizeof(moved)) {
      memcpy(moved, target.path, length);
      if (length > 1)
        moved[length++] = '/';
      strcpy(moved + length, base);
      vfs_perm_rename(source.path, moved);
    }
  }
  return status;
}

static bool short_has(const struct fs_directory_entry *entries, uint32_t count,
                      const char *name) {
  char clipped[FS_DIRECTORY_NAME_CAPACITY];
  copy_name(clipped, name);
  for (uint32_t i = 0; i < count; i++)
    if (strcmp(entries[i].name, clipped) == 0)
      return true;
  return false;
}

static bool long_has(const struct fs_directory_entry_long *entries,
                     uint32_t count, const char *name) {
  for (uint32_t i = 0; i < count; i++)
    if (strcmp(entries[i].name, name) == 0)
      return true;
  return false;
}

int32_t vfs_list(const char *path, struct fs_directory_entry *entries,
                 uint32_t capacity) {
  MUTEX_SCOPE(&vfs_mutex);
  if (!entries || capacity == 0)
    return FS_ERROR_INVALID;
  struct vfs_resolved resolved;
  int32_t status = resolve(path, &resolved);
  if (status < 0)
    return status;
  const struct vfs_fs_ops *ops = resolved.mount->ops;
  int32_t backend = FS_ERROR_UNSUPPORTED;
  if (ops->list && mount_ready(resolved.mount))
    backend = ops->list(resolved.mount->context, resolved.relative, entries,
                        capacity);
  const char *children[VFS_MOUNT_MAX + 8];
  uint32_t child_count = vfs_mount_children(resolved.path, children,
                                            VFS_MOUNT_MAX);
  child_count += virtual_children(resolved.path, children + child_count, 8);
  bool is_root = path_equals(resolved.path, "/");
  if (backend < 0 && !is_root && !child_count && !is_virtual_dir(resolved.path))
    return backend;
  uint32_t count = backend > 0 ? (uint32_t)backend : 0;
  for (uint32_t i = 0; i < child_count && count < capacity; i++) {
    if (short_has(entries, count, children[i]))
      continue;
    copy_name(entries[count].name, children[i]);
    entries[count].size = 0;
    entries[count].attributes = FS_ATTRIBUTE_DIRECTORY;
    count++;
  }
  if (is_root) {
    static const char *const log_names[] = {"dmesg.txt", "kernel.log"};
    for (uint32_t i = 0; i < 2 && count < capacity; i++) {
      if (short_has(entries, count, log_names[i]))
        continue;
      copy_name(entries[count].name, log_names[i]);
      entries[count].size = klog_visible_size();
      entries[count].attributes = 0;
      count++;
    }
  }
  return (int32_t)count;
}

int32_t vfs_list_long(const char *path, struct fs_directory_entry_long *entries,
                      uint32_t capacity) {
  MUTEX_SCOPE(&vfs_mutex);
  if (!entries || capacity == 0)
    return FS_ERROR_INVALID;
  struct vfs_resolved resolved;
  int32_t status = resolve(path, &resolved);
  if (status < 0)
    return status;
  const struct vfs_fs_ops *ops = resolved.mount->ops;
  int32_t backend = FS_ERROR_UNSUPPORTED;
  if (ops->list_long && mount_ready(resolved.mount))
    backend = ops->list_long(resolved.mount->context, resolved.relative,
                             entries, capacity);
  const char *children[VFS_MOUNT_MAX + 8];
  uint32_t child_count = vfs_mount_children(resolved.path, children,
                                            VFS_MOUNT_MAX);
  child_count += virtual_children(resolved.path, children + child_count, 8);
  bool is_root = path_equals(resolved.path, "/");
  if (backend < 0 && !is_root && !child_count && !is_virtual_dir(resolved.path))
    return backend;
  uint32_t count = backend > 0 ? (uint32_t)backend : 0;
  for (uint32_t i = 0; i < child_count && count < capacity; i++) {
    if (long_has(entries, count, children[i]))
      continue;
    copy_name_long(entries[count].name, children[i]);
    entries[count].size = 0;
    entries[count].attributes = FS_ATTRIBUTE_DIRECTORY;
    count++;
  }
  if (is_root) {
    static const char *const log_names[] = {"dmesg.txt", "kernel.log"};
    for (uint32_t i = 0; i < 2 && count < capacity; i++) {
      if (long_has(entries, count, log_names[i]))
        continue;
      copy_name_long(entries[count].name, log_names[i]);
      entries[count].size = klog_visible_size();
      entries[count].attributes = 0;
      count++;
    }
  }
  return (int32_t)count;
}

int32_t vfs_create_file(const char *path) {
  MUTEX_SCOPE(&vfs_mutex);
  struct vfs_resolved resolved;
  int32_t status = resolve(path, &resolved);
  if (status < 0)
    return status;
  if (is_klog_path(resolved.path))
    return 0;
  if (vfs_mount_at(resolved.path))
    return FS_ERROR_EXISTS;
  status = check_mutation(resolved.mount, resolved.mount->ops->create_file);
  if (status < 0)
    return status;
  status = check_parent_write(resolved.path);
  if (status < 0)
    return status;
  status = resolved.mount->ops->create_file(resolved.mount->context,
                                            resolved.relative);
  if (status >= 0)
    own_created(resolved.path, false);
  return status;
}

static uint32_t raw_log_base_lba;
static uint32_t raw_log_next_sector;
static bool raw_log_inited;
static int32_t raw_log_write(const void *buffer, uint32_t count);
static int32_t raw_log_truncate(void);

int32_t vfs_write_file(const char *path, const void *buffer, uint32_t count) {
  MUTEX_SCOPE(&vfs_mutex);
  struct vfs_resolved resolved;
  int32_t status = resolve(path, &resolved);
  if (status < 0)
    return status;
  if (is_klog_path(resolved.path)) {
    if (count == 0)
      raw_log_truncate();
    int32_t r = ext2_is_mounted()
                    ? ext2_write_file(resolved.path, buffer, count)
                    : FS_ERROR_NOT_FOUND;
    if (r >= 0) {
      (void)raw_log_write(buffer, count);
      return r;
    }
    (void)raw_log_write(buffer, count);
    return (int32_t)count;
  }
  status = check_mutation(resolved.mount, resolved.mount->ops->write_file);
  if (status < 0)
    return status;
  bool created;
  status = check_write_target(&resolved, &created);
  if (status < 0)
    return status;
  status = resolved.mount->ops->write_file(resolved.mount->context,
                                           resolved.relative, buffer, count);
  if (status >= 0 && created)
    own_created(resolved.path, false);
  return status;
}

int32_t vfs_append_file(const char *path, const void *buffer, uint32_t count) {
  MUTEX_SCOPE(&vfs_mutex);
  struct vfs_resolved resolved;
  int32_t status = resolve(path, &resolved);
  if (status < 0)
    return status;
  if (is_klog_path(resolved.path)) {
    int32_t r = ext2_is_mounted()
                    ? ext2_append_file(resolved.path, buffer, count)
                    : FS_ERROR_NOT_FOUND;
    if (r >= 0) {
      (void)raw_log_write(buffer, count);
      return (int32_t)count;
    }
    (void)raw_log_write(buffer, count);
    return (int32_t)count;
  }
  status = check_mutation(resolved.mount, resolved.mount->ops->append_file);
  if (status < 0)
    return status;
  bool created;
  status = check_write_target(&resolved, &created);
  if (status < 0)
    return status;
  int32_t appended = resolved.mount->ops->append_file(
      resolved.mount->context, resolved.relative, buffer, count);
  if (appended >= 0 && created)
    own_created(resolved.path, false);
  return appended >= 0 ? (int32_t)count : appended;
}

int32_t vfs_create_directory(const char *path) {
  MUTEX_SCOPE(&vfs_mutex);
  struct vfs_resolved resolved;
  int32_t status = resolve(path, &resolved);
  if (status < 0)
    return status;
  if (vfs_mount_at(resolved.path))
    return FS_ERROR_EXISTS;
  status = check_mutation(resolved.mount, resolved.mount->ops->create_directory);
  if (status < 0)
    return status;
  status = check_parent_write(resolved.path);
  if (status < 0)
    return status;
  status = resolved.mount->ops->create_directory(resolved.mount->context,
                                                 resolved.relative);
  if (status >= 0)
    own_created(resolved.path, true);
  return status;
}

static int32_t resolve_existing(const char *path, struct vfs_resolved *resolved,
                                bool *directory) {
  int32_t status = resolve(path, resolved);
  if (status < 0)
    return status;
  struct file_stat_info info;
  const struct vfs_fs_ops *ops = resolved->mount->ops;
  if (!ops->stat || !mount_ready(resolved->mount) ||
      ops->stat(resolved->mount->context, resolved->relative, &info) < 0) {
    if (!is_virtual_dir(resolved->path))
      return FS_ERROR_NOT_FOUND;
    *directory = true;
    return 0;
  }
  *directory = info.is_directory != 0;
  return 0;
}

int32_t vfs_stat_ex(const char *path, struct file_stat_ex *out) {
  MUTEX_SCOPE(&vfs_mutex);
  if (!out)
    return FS_ERROR_INVALID;
  struct vfs_resolved resolved;
  int32_t status = resolve(path, &resolved);
  if (status < 0)
    return status;
  const struct vfs_fs_ops *ops = resolved.mount->ops;
  memset(out, 0, sizeof(*out));
  bool filled = false;
  if (ops->stat_ex && mount_ready(resolved.mount) &&
      !is_klog_path(resolved.path)) {
    status = ops->stat_ex(resolved.mount->context, resolved.relative, out);
    if (status < 0 && !is_virtual_dir(resolved.path))
      return status;
    filled = status >= 0;
  }
  if (!filled) {
    struct file_stat_info info;
    if (is_klog_path(resolved.path)) {
      out->size = klog_total_bytes();
    } else {
      const void *data = 0;
      uint32_t size = 0;
      if (overlay_find(resolved.path, &data, &size)) {
        out->size = size;
      } else if (ops->stat && mount_ready(resolved.mount) &&
                 ops->stat(resolved.mount->context, resolved.relative,
                           &info) >= 0) {
        out->size = info.size;
        out->is_directory = info.is_directory;
      } else if (is_virtual_dir(resolved.path)) {
        out->is_directory = 1;
      } else {
        return FS_ERROR_NOT_FOUND;
      }
    }
  }
  struct vfs_perm_meta meta;
  effective_meta(resolved.path, out->is_directory != 0, &meta);
  out->mode = meta.mode;
  out->uid = meta.uid;
  out->gid = meta.gid;
  return 0;
}

int32_t vfs_chmod(const char *path, uint16_t mode) {
  MUTEX_SCOPE(&vfs_mutex);
  struct vfs_resolved resolved;
  bool directory;
  int32_t status = resolve_existing(path, &resolved, &directory);
  if (status < 0)
    return status;
  if (resolved.mount->flags & VFS_MOUNT_READ_ONLY)
    return FS_ERROR_READ_ONLY;
  struct vfs_perm_meta meta;
  effective_meta(resolved.path, directory, &meta);
  uint32_t uid = process_current_uid();
  if (uid != 0 && uid != meta.uid)
    return FS_ERROR_DENIED;
  meta.mode = mode & VFS_MODE_MASK;
  return store_meta(resolved.path, &meta);
}

int32_t vfs_chown(const char *path, uint32_t uid, uint32_t gid) {
  MUTEX_SCOPE(&vfs_mutex);
  struct vfs_resolved resolved;
  bool directory;
  int32_t status = resolve_existing(path, &resolved, &directory);
  if (status < 0)
    return status;
  if (resolved.mount->flags & VFS_MOUNT_READ_ONLY)
    return FS_ERROR_READ_ONLY;
  if (process_current_uid() != 0)
    return FS_ERROR_DENIED;
  struct vfs_perm_meta meta;
  effective_meta(resolved.path, directory, &meta);
  if (uid != USER_ID_NONE)
    meta.uid = uid;
  if (gid != USER_ID_NONE)
    meta.gid = gid;
  return store_meta(resolved.path, &meta);
}

int32_t vfs_get_perm(const char *path, struct file_perm_info *out) {
  MUTEX_SCOPE(&vfs_mutex);
  if (!out)
    return FS_ERROR_INVALID;
  struct vfs_resolved resolved;
  bool directory;
  int32_t status = resolve_existing(path, &resolved, &directory);
  if (status < 0)
    return status;
  struct vfs_perm_meta meta;
  effective_meta(resolved.path, directory, &meta);
  out->mode = meta.mode;
  out->uid = meta.uid;
  out->gid = meta.gid;
  out->reserved = directory ? 1 : 0;
  return 0;
}

int32_t vfs_user_lookup(const char *name, uint32_t uid,
                        struct user_info *out) {
  if (!out)
    return FS_ERROR_INVALID;
  int32_t handle = vfs_open("/etc/passwd");
  if (handle < 0)
    return handle;
  static char passwd_text[4096];
  int32_t total = 0;
  for (;;) {
    int32_t got = vfs_read(handle, passwd_text + total,
                           (uint32_t)(sizeof(passwd_text) - 1 - total));
    if (got <= 0)
      break;
    total += got;
    if ((uint32_t)total >= sizeof(passwd_text) - 1)
      break;
  }
  vfs_close(handle);
  passwd_text[total] = '\0';
  struct user_entry entry;
  bool found = name ? user_lookup_name(passwd_text, name, &entry)
                    : user_lookup_uid(passwd_text, uid, &entry);
  if (!found)
    return FS_ERROR_NOT_FOUND;
  memset(out, 0, sizeof(*out));
  strncpy(out->name, entry.name, sizeof(out->name) - 1);
  strncpy(out->home, entry.home, sizeof(out->home) - 1);
  strncpy(out->shell, entry.shell, sizeof(out->shell) - 1);
  out->uid = entry.uid;
  out->gid = entry.gid;
  return 0;
}

int32_t vfs_mounts_info(uint32_t index, struct vfs_mount_info *out) {
  MUTEX_SCOPE(&vfs_mutex);
  ensure_mounts();
  const struct vfs_mount *mount = vfs_mount_get(index);
  if (!mount || !out)
    return FS_ERROR_NOT_FOUND;
  strncpy(out->path, mount->path, sizeof(out->path) - 1);
  out->path[sizeof(out->path) - 1] = '\0';
  strncpy(out->source, mount->source, sizeof(out->source) - 1);
  out->source[sizeof(out->source) - 1] = '\0';
  strncpy(out->type, mount->ops->name, sizeof(out->type) - 1);
  out->type[sizeof(out->type) - 1] = '\0';
  out->flags = mount->flags;
  return 0;
}

int32_t vfs_format_device(const char *device_name,
                          const char *serial_confirmation,
                          const char *erase_confirmation) {
  MUTEX_SCOPE(&vfs_mutex);
  return vfs_format_device_ex(device_name, serial_confirmation,
                              erase_confirmation, VFS_FS_EXT2);
}

int32_t vfs_format_device_force(const char *device_name,
                                const char *serial_confirmation) {
  MUTEX_SCOPE(&vfs_mutex);
  return vfs_format_device_ex(device_name, serial_confirmation, "ERASE",
                              VFS_FS_EXT2);
}

int32_t vfs_format_device_ex(const char *device_name,
                             const char *serial_confirmation,
                             const char *erase_confirmation, uint8_t fs_type) {
  MUTEX_SCOPE(&vfs_mutex);
  if (!device_name || !device_name[0])
    return FS_ERROR_INVALID;
  if (fs_type != VFS_FS_EXT2)
    return FS_ERROR_UNSUPPORTED;
  return ext2_format_device(device_name, serial_confirmation,
                            erase_confirmation);
}

int32_t vfs_install_device(const char *device_name,
                           const char *serial_confirmation,
                           install_progress_callback callback) {
  MUTEX_SCOPE(&vfs_mutex);
  return installer_install_device(device_name, serial_confirmation, callback);
}

static bool raw_log_init(void) {
  if (raw_log_inited)
    return true;
  uint32_t n = block_device_count();
  if (n == 0)
    return false;
  struct storage_device_info info;
  bool found = false;
  for (uint32_t i = 0; i < n; i++)
    if (block_device_get_info(i, &info) && info.selected) {
      found = true;
      break;
    }
  if (!found)
    if (!block_device_get_info(0, &info))
      return false;
  if (info.sector_count < 4096)
    return false;
  raw_log_base_lba = 1024;
  if (raw_log_base_lba + 2048 > info.sector_count)
    raw_log_base_lba = info.sector_count - 2048;
  raw_log_next_sector = 1;
  raw_log_inited = true;
  uint8_t hdr[512];
  hdr[0] = 'K';
  hdr[1] = 'L';
  hdr[2] = 'O';
  hdr[3] = 'G';
  hdr[4] = 0;
  hdr[5] = 0;
  hdr[6] = 0;
  hdr[7] = 0;
  for (uint32_t i = 8; i < 512; i++)
    hdr[i] = 0;
  (void)block_device_write(raw_log_base_lba, hdr);
  return true;
}

static int32_t raw_log_truncate(void) {
  if (!raw_log_init())
    return FS_ERROR_INVALID;
  raw_log_next_sector = 1;
  uint8_t hdr[512];
  hdr[0] = 'K';
  hdr[1] = 'L';
  hdr[2] = 'O';
  hdr[3] = 'G';
  for (uint32_t i = 4; i < 512; i++)
    hdr[i] = 0;
  if (!block_device_write(raw_log_base_lba, hdr))
    return FS_ERROR_IO;
  return 0;
}

static int32_t raw_log_write(const void *buffer, uint32_t count) {
  if (!buffer && count)
    return FS_ERROR_INVALID;
  if (count == 0)
    return 0;
  if (!raw_log_init())
    return FS_ERROR_INVALID;
  const uint8_t *src = (const uint8_t *)buffer;
  uint32_t written = 0;
  uint8_t sector[512];
  while (count > 0) {
    if (raw_log_next_sector >= 2048)
      break;
    uint32_t to_copy = count > 512 ? 512 : count;
    memset(sector, 0, 512);
    memcpy(sector, src, to_copy);
    if (!block_device_write(raw_log_base_lba + raw_log_next_sector, sector))
      break;
    raw_log_next_sector++;
    src += to_copy;
    count -= to_copy;
    written += to_copy;
    if (count == 0) {
      uint8_t hdr[512];
      if (block_device_read(raw_log_base_lba, hdr)) {
        hdr[4] = (uint8_t)(raw_log_next_sector & 0xFF);
        hdr[5] = (uint8_t)((raw_log_next_sector >> 8) & 0xFF);
        hdr[6] = (uint8_t)((raw_log_next_sector >> 16) & 0xFF);
        hdr[7] = (uint8_t)((raw_log_next_sector >> 24) & 0xFF);
        (void)block_device_write(raw_log_base_lba, hdr);
      }
    }
  }
  return written ? (int32_t)written : FS_ERROR_IO;
}

int32_t vfs_save_klog_to_device(const char *device, const char *path) {
  MUTEX_SCOPE(&vfs_mutex);
  if (!device || !device[0] || !path || !path[0])
    return FS_ERROR_INVALID;
  int32_t dev_idx = block_device_find(device);
  if (dev_idx < 0)
    return FS_ERROR_NOT_FOUND;
  struct storage_device_info info;
  if (!block_device_get_info((uint32_t)dev_idx, &info))
    return FS_ERROR_INVALID;
  if (!info.operational || !info.writable)
    return FS_ERROR_READ_ONLY;
  const char *prev_root = vfs_root_device_name();
  char prev_device[32] = {0};
  if (prev_root) {
    strncpy(prev_device, prev_root, sizeof(prev_device) - 1);
    prev_device[sizeof(prev_device) - 1] = '\0';
  }
  bool mounted = false;
  if (prev_root && strcmp(prev_root, device) == 0)
    mounted = vfs_is_root_mounted();
  else {
    if (block_device_select((uint32_t)dev_idx)) {
      if (ext2_mount_specific(device)) {
        set_root_ops(VFS_FS_EXT2);
        mounted = true;
      } else
        mounted = vfs_mount_root();
    }
  }
  if (!mounted)
    return FS_ERROR_IO;
  uint64_t total = klog_total_bytes();
  uint64_t cursor = total > (8 * 1024 * 1024) ? total - 8 * 1024 * 1024 : 0;
  static char log_buffer[64 * 1024];
  uint64_t write_cursor = cursor;
  (void)vfs_delete(path);
  int32_t fd = vfs_open(path);
  if (fd < 0) {
    if (vfs_create_file(path) < 0) {
      if (prev_root && strcmp(prev_root, device) != 0 && prev_device[0]) {
        int32_t prev_idx = block_device_find(prev_device);
        if (prev_idx >= 0) {
          block_device_select((uint32_t)prev_idx);
          vfs_mount_root();
        }
      }
      return fd;
    }
    fd = vfs_open(path);
    if (fd < 0)
      return fd;
  }
  vfs_close(fd);
  uint32_t total_written = 0;
  bool data_lost = false;
  while (write_cursor < total) {
    uint32_t to_read = sizeof(log_buffer);
    if (write_cursor + to_read > total)
      to_read = (uint32_t)(total - write_cursor);
    uint32_t got =
        klog_read_since(&write_cursor, log_buffer, to_read, &data_lost);
    if (got == 0)
      break;
    int32_t wr = vfs_append_file(path, log_buffer, got);
    if (wr < 0) {
      wr = vfs_write_file(path, log_buffer, got);
      if (wr < 0)
        break;
    }
    total_written += got;
    if (data_lost)
      break;
  }
  if (prev_root && strcmp(prev_root, device) != 0 && prev_device[0]) {
    int32_t prev_idx = block_device_find(prev_device);
    if (prev_idx >= 0) {
      block_device_select((uint32_t)prev_idx);
      vfs_mount_root();
    }
  }
  if (total_written == 0)
    return FS_ERROR_IO;
  return (int32_t)total_written;
}

int32_t vfs_ext2_stat(const char *path, struct ext2_stat_info *out) {
  MUTEX_SCOPE(&vfs_mutex);
  if (vfs_active_fs != VFS_FS_EXT2 || !ext2_is_mounted())
    return FS_ERROR_UNSUPPORTED;
  return ext2_stat_path(path, out);
}
int32_t vfs_ext2_inode(uint32_t ino, struct ext2_stat_info *out) {
  MUTEX_SCOPE(&vfs_mutex);
  if (vfs_active_fs != VFS_FS_EXT2 || !ext2_is_mounted())
    return FS_ERROR_UNSUPPORTED;
  return ext2_stat_ino(ino, out);
}
int32_t vfs_ext2_super(struct ext2_super_info *out) {
  MUTEX_SCOPE(&vfs_mutex);
  if (vfs_active_fs != VFS_FS_EXT2 || !ext2_is_mounted())
    return FS_ERROR_UNSUPPORTED;
  return ext2_super_info(out);
}
int32_t vfs_ext2_blocks(const char *path, struct ext2_blocks_info *out) {
  MUTEX_SCOPE(&vfs_mutex);
  if (vfs_active_fs != VFS_FS_EXT2 || !ext2_is_mounted())
    return FS_ERROR_UNSUPPORTED;
  return ext2_file_blocks(path, out);
}
int32_t vfs_ext2_inode_blocks(uint32_t ino, struct ext2_blocks_info *out) {
  MUTEX_SCOPE(&vfs_mutex);
  if (vfs_active_fs != VFS_FS_EXT2 || !ext2_is_mounted())
    return FS_ERROR_UNSUPPORTED;
  return ext2_inode_blocks(ino, out);
}

static bool selftest_stat(const char *path, bool directory) {
  struct file_stat_info info;
  if (vfs_stat(path, &info) < 0) {
    klogf(KLOG_WARN, "vfs: selftest %s missing", path);
    return false;
  }
  if ((info.is_directory != 0) != directory) {
    klogf(KLOG_WARN, "vfs: selftest %s wrong type", path);
    return false;
  }
  return true;
}

static bool selftest_read(const char *path, const char *expected_prefix) {
  int32_t fd = vfs_open(path);
  if (fd < 0) {
    klogf(KLOG_WARN, "vfs: selftest open %s failed %d", path, fd);
    return false;
  }
  char buffer[64];
  int32_t count = vfs_read(fd, buffer, sizeof(buffer) - 1);
  (void)vfs_close(fd);
  if (count < 0) {
    klogf(KLOG_WARN, "vfs: selftest read %s failed %d", path, count);
    return false;
  }
  buffer[count] = '\0';
  if (expected_prefix && strncmp(buffer, expected_prefix, strlen(expected_prefix)) != 0) {
    klogf(KLOG_WARN, "vfs: selftest %s content mismatch", path);
    return false;
  }
  return true;
}

static bool selftest_tmp(void) {
  const char *path = "/tmp/.vfs-selftest";
  if (vfs_write_file(path, "purec", 5) != 5) {
    klog(KLOG_WARN, "vfs: selftest /tmp write failed");
    return false;
  }
  bool ok = selftest_read(path, "purec");
  (void)vfs_delete(path);
  return ok;
}

void vfs_selftest(void) {
  uint32_t failed = 0;
  failed += !selftest_stat("/", true);
  failed += !selftest_stat("/bin/ls", false);
  failed += !selftest_stat("/usr/bin/terminal", false);
  failed += !selftest_stat("/bin/program/terminal", false);
  failed += !selftest_stat("/usr/share/applications", true);
  failed += !selftest_stat("/sbin/init", false);
  failed += !selftest_stat("/etc/hostname", false);
  failed += !selftest_stat("/etc", true);
  failed += !selftest_stat("/dev", true);
  failed += !selftest_stat("/dev/null", false);
  failed += !selftest_stat("/proc", true);
  failed += !selftest_stat("/sys", true);
  failed += !selftest_read("/sys/kernel/version", "PureC OS");
  failed += !selftest_stat("/tmp", true);
  failed += !selftest_stat("/home", true);
  failed += !selftest_stat("/var/log", true);
  failed += !selftest_stat("/mnt", true);
  failed += !selftest_read("/proc/version", "PureC OS");
  failed += !selftest_read("/etc/hostname", "purec");
  failed += !selftest_read("/dev/zero", 0);
  failed += !selftest_tmp();
  static struct fs_directory_entry_long entries[64];
  int32_t count = vfs_list_long("/", entries, 64);
  if (count < 10) {
    klogf(KLOG_WARN, "vfs: selftest root listing only %d entries", count);
    failed++;
  }
  if (failed)
    klogf(KLOG_WARN, "vfs: selftest FAILED checks=%u", failed);
  else
    klog(KLOG_OK, "vfs: selftest passed");
}
