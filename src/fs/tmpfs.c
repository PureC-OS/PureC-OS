#include "vfs_backends.h"
#include "../lib/string.h"
#include "../mm/pmm.h"

#define TMPFS_MAX_NODES 256
#define TMPFS_MAX_OPEN 32
#define TMPFS_NAME_MAX 63
#define TMPFS_MAX_FILE (8u * 1024u * 1024u)
#define TMPFS_PAGE 4096u
#define TMPFS_ROOT 0

struct node {
  bool used;
  bool directory;
  int16_t parent;
  char name[TMPFS_NAME_MAX + 1];
  uint8_t *data;
  uint64_t physical;
  uint64_t pages;
  uint32_t size;
};

struct handle {
  bool used;
  int16_t node;
  uint32_t position;
};

struct tmpfs {
  struct node nodes[TMPFS_MAX_NODES];
  struct handle handles[TMPFS_MAX_OPEN];
  uint64_t used_pages;
  uint64_t page_limit;
};

void *vfs_tmpfs_create(void) {
  uint64_t pages = (sizeof(struct tmpfs) + TMPFS_PAGE - 1) / TMPFS_PAGE;
  uint64_t physical = pmm_allocate_contiguous(pages);
  if (!physical)
    return 0;
  struct tmpfs *fs = pmm_physical_to_virtual(physical);
  memset(fs, 0, (size_t)(pages * TMPFS_PAGE));
  fs->nodes[TMPFS_ROOT].used = true;
  fs->nodes[TMPFS_ROOT].directory = true;
  fs->nodes[TMPFS_ROOT].parent = TMPFS_ROOT;
  uint64_t quarter = pmm_total_bytes() / 4 / TMPFS_PAGE;
  uint64_t cap = (64ULL * 1024 * 1024) / TMPFS_PAGE;
  fs->page_limit = quarter < cap ? quarter : cap;
  if (fs->page_limit < 16)
    fs->page_limit = 16;
  return fs;
}

static int32_t find_child(struct tmpfs *fs, int32_t parent, const char *name, uint32_t length) {
  for (int32_t i = 0; i < TMPFS_MAX_NODES; i++) {
    if (!fs->nodes[i].used || i == TMPFS_ROOT || fs->nodes[i].parent != parent)
      continue;
    if (strlen(fs->nodes[i].name) == length && memcmp(fs->nodes[i].name, name, length) == 0)
      return i;
  }
  return -1;
}

static int32_t lookup(struct tmpfs *fs, const char *path) {
  if (!path || path[0] != '/')
    return FS_ERROR_INVALID;
  int32_t current = TMPFS_ROOT;
  const char *cursor = path;
  while (*cursor) {
    while (*cursor == '/')
      cursor++;
    if (!*cursor)
      break;
    const char *start = cursor;
    while (*cursor && *cursor != '/')
      cursor++;
    if (!fs->nodes[current].directory)
      return FS_ERROR_NOT_DIR;
    current = find_child(fs, current, start, (uint32_t)(cursor - start));
    if (current < 0)
      return FS_ERROR_NOT_FOUND;
  }
  return current;
}

static int32_t split_parent(struct tmpfs *fs, const char *path, int32_t *parent, const char **leaf,
                            uint32_t *leaf_length) {
  if (!path || path[0] != '/')
    return FS_ERROR_INVALID;
  uint32_t length = (uint32_t)strlen(path);
  while (length > 1 && path[length - 1] == '/')
    length--;
  uint32_t slash = length;
  while (slash > 0 && path[slash - 1] != '/')
    slash--;
  uint32_t name_length = length - slash;
  if (!name_length)
    return FS_ERROR_INVALID;
  if (name_length > TMPFS_NAME_MAX)
    return FS_ERROR_INVALID;
  char directory[512];
  uint32_t directory_length = slash > 1 ? slash - 1 : 1;
  if (directory_length >= sizeof(directory))
    return FS_ERROR_INVALID;
  memcpy(directory, path, directory_length);
  directory[directory_length] = '\0';
  int32_t found = lookup(fs, directory);
  if (found < 0)
    return found;
  if (!fs->nodes[found].directory)
    return FS_ERROR_NOT_DIR;
  *parent = found;
  *leaf = path + slash;
  *leaf_length = name_length;
  return 0;
}

static int32_t allocate_node(struct tmpfs *fs, int32_t parent, const char *name, uint32_t length,
                             bool directory) {
  for (int32_t i = 1; i < TMPFS_MAX_NODES; i++) {
    if (fs->nodes[i].used)
      continue;
    memset(&fs->nodes[i], 0, sizeof(fs->nodes[i]));
    fs->nodes[i].used = true;
    fs->nodes[i].directory = directory;
    fs->nodes[i].parent = (int16_t)parent;
    memcpy(fs->nodes[i].name, name, length);
    fs->nodes[i].name[length] = '\0';
    return i;
  }
  return FS_ERROR_NO_SPACE;
}

static void release_data(struct tmpfs *fs, struct node *node) {
  if (node->physical)
    pmm_free_contiguous(node->physical, node->pages);
  fs->used_pages -= node->pages;
  node->data = 0;
  node->physical = 0;
  node->pages = 0;
  node->size = 0;
}

static int32_t reserve(struct tmpfs *fs, struct node *node, uint32_t needed) {
  if (needed > TMPFS_MAX_FILE)
    return FS_ERROR_NO_SPACE;
  uint64_t pages = ((uint64_t)needed + TMPFS_PAGE - 1) / TMPFS_PAGE;
  if (pages <= node->pages)
    return 0;
  if (fs->used_pages - node->pages + pages > fs->page_limit)
    return FS_ERROR_NO_SPACE;
  uint64_t physical = pmm_allocate_contiguous(pages);
  if (!physical)
    return FS_ERROR_NO_SPACE;
  uint8_t *data = pmm_physical_to_virtual(physical);
  memset(data, 0, (size_t)(pages * TMPFS_PAGE));
  if (node->size)
    memcpy(data, node->data, node->size);
  if (node->physical)
    pmm_free_contiguous(node->physical, node->pages);
  fs->used_pages = fs->used_pages - node->pages + pages;
  node->data = data;
  node->physical = physical;
  node->pages = pages;
  return 0;
}

static bool is_open(struct tmpfs *fs, int32_t node) {
  for (int i = 0; i < TMPFS_MAX_OPEN; i++)
    if (fs->handles[i].used && fs->handles[i].node == node)
      return true;
  return false;
}

static int32_t tmpfs_open(void *context, const char *path) {
  struct tmpfs *fs = context;
  int32_t node = lookup(fs, path);
  if (node < 0)
    return node;
  if (fs->nodes[node].directory)
    return FS_ERROR_NOT_FILE;
  for (int32_t i = 0; i < TMPFS_MAX_OPEN; i++) {
    if (fs->handles[i].used)
      continue;
    fs->handles[i].used = true;
    fs->handles[i].node = (int16_t)node;
    fs->handles[i].position = 0;
    return i;
  }
  return FS_ERROR_NO_SPACE;
}

static struct handle *get_handle(struct tmpfs *fs, int32_t descriptor) {
  if (descriptor < 0 || descriptor >= TMPFS_MAX_OPEN || !fs->handles[descriptor].used)
    return 0;
  return &fs->handles[descriptor];
}

static int32_t tmpfs_read(void *context, int32_t descriptor, void *buffer,
                          uint32_t count) {
  struct tmpfs *fs = context;
  struct handle *handle = get_handle(fs, descriptor);
  if (!handle)
    return FS_ERROR_INVALID;
  struct node *node = &fs->nodes[handle->node];
  if (handle->position >= node->size)
    return 0;
  uint32_t remaining = node->size - handle->position;
  uint32_t amount = count < remaining ? count : remaining;
  memcpy(buffer, node->data + handle->position, amount);
  handle->position += amount;
  return (int32_t)amount;
}

static int64_t tmpfs_seek(void *context, int32_t descriptor, int64_t offset,
                          uint32_t whence) {
  struct tmpfs *fs = context;
  struct handle *handle = get_handle(fs, descriptor);
  if (!handle)
    return FS_ERROR_INVALID;
  int64_t base = whence == SEEK_CUR   ? (int64_t)handle->position
                 : whence == SEEK_END ? (int64_t)fs->nodes[handle->node].size
                                      : 0;
  int64_t target = base + offset;
  if (target < 0)
    return FS_ERROR_INVALID;
  if (target > (int64_t)fs->nodes[handle->node].size)
    target = fs->nodes[handle->node].size;
  handle->position = (uint32_t)target;
  return target;
}

static int32_t tmpfs_close(void *context, int32_t descriptor) {
  struct tmpfs *fs = context;
  struct handle *handle = get_handle(fs, descriptor);
  if (!handle)
    return FS_ERROR_INVALID;
  handle->used = false;
  return 0;
}

static int32_t tmpfs_stat(void *context, const char *path,
                          struct file_stat_info *out) {
  struct tmpfs *fs = context;
  int32_t node = lookup(fs, path);
  if (node < 0)
    return node;
  out->size = fs->nodes[node].size;
  out->is_directory = fs->nodes[node].directory ? 1 : 0;
  out->reserved = FS_NODE_FILE;
  return 0;
}

static int32_t list_directory(struct tmpfs *fs, const char *path, void *entries,
                              uint32_t capacity, bool long_names) {
  int32_t directory = lookup(fs, path);
  if (directory < 0)
    return directory;
  if (!fs->nodes[directory].directory)
    return FS_ERROR_NOT_DIR;
  uint32_t count = 0;
  for (int32_t i = 1; i < TMPFS_MAX_NODES && count < capacity; i++) {
    if (!fs->nodes[i].used || fs->nodes[i].parent != directory)
      continue;
    uint8_t attributes = fs->nodes[i].directory ? FS_ATTRIBUTE_DIRECTORY : 0;
    if (long_names) {
      struct fs_directory_entry_long *entry = &((struct fs_directory_entry_long *)entries)[count];
      strncpy(entry->name, fs->nodes[i].name, FS_LONG_NAME_CAPACITY - 1);
      entry->name[FS_LONG_NAME_CAPACITY - 1] = '\0';
      entry->size = fs->nodes[i].size;
      entry->attributes = attributes;
      memset(entry->reserved, 0, sizeof(entry->reserved));
    } else {
      struct fs_directory_entry *entry = &((struct fs_directory_entry *)entries)[count];
      strncpy(entry->name, fs->nodes[i].name, FS_DIRECTORY_NAME_CAPACITY - 1);
      entry->name[FS_DIRECTORY_NAME_CAPACITY - 1] = '\0';
      entry->size = fs->nodes[i].size;
      entry->attributes = attributes;
    }
    count++;
  }
  return (int32_t)count;
}

static int32_t tmpfs_list(void *context, const char *path,
                          struct fs_directory_entry *entries, uint32_t capacity) {
  struct tmpfs *fs = context;
  return list_directory(fs, path, entries, capacity, false);
}

static int32_t tmpfs_list_long(void *context, const char *path,
                               struct fs_directory_entry_long *entries,
                               uint32_t capacity) {
  struct tmpfs *fs = context;
  return list_directory(fs, path, entries, capacity, true);
}

static int32_t create_entry(struct tmpfs *fs, const char *path, bool directory) {
  int32_t parent;
  const char *leaf;
  uint32_t length;
  int32_t status = split_parent(fs, path, &parent, &leaf, &length);
  if (status < 0)
    return status;
  if (find_child(fs, parent, leaf, length) >= 0)
    return FS_ERROR_EXISTS;
  status = allocate_node(fs, parent, leaf, length, directory);
  return status < 0 ? status : 0;
}

static int32_t tmpfs_create_file(void *context, const char *path) {
  struct tmpfs *fs = context;
  return create_entry(fs, path, false);
}

static int32_t tmpfs_create_directory(void *context, const char *path) {
  struct tmpfs *fs = context;
  return create_entry(fs, path, true);
}

static bool has_children(struct tmpfs *fs, int32_t directory) {
  for (int32_t i = 1; i < TMPFS_MAX_NODES; i++)
    if (fs->nodes[i].used && fs->nodes[i].parent == directory)
      return true;
  return false;
}

static int32_t tmpfs_unlink(void *context, const char *path) {
  struct tmpfs *fs = context;
  int32_t node = lookup(fs, path);
  if (node < 0)
    return node;
  if (node == TMPFS_ROOT)
    return FS_ERROR_BUSY;
  if (fs->nodes[node].directory && has_children(fs, node))
    return FS_ERROR_NOT_BLANK;
  if (is_open(fs, node))
    return FS_ERROR_BUSY;
  release_data(fs, &fs->nodes[node]);
  memset(&fs->nodes[node], 0, sizeof(fs->nodes[node]));
  return 0;
}

static bool valid_name(const char *name) {
  uint32_t length = (uint32_t)strlen(name);
  if (!length || length > TMPFS_NAME_MAX)
    return false;
  for (uint32_t i = 0; i < length; i++)
    if (name[i] == '/')
      return false;
  return true;
}

static int32_t tmpfs_rename(void *context, const char *path, const char *new_name) {
  struct tmpfs *fs = context;
  int32_t node = lookup(fs, path);
  if (node < 0)
    return node;
  if (node == TMPFS_ROOT)
    return FS_ERROR_BUSY;
  if (!valid_name(new_name))
    return FS_ERROR_INVALID;
  uint32_t length = (uint32_t)strlen(new_name);
  int32_t existing = find_child(fs, fs->nodes[node].parent, new_name, length);
  if (existing >= 0 && existing != node)
    return FS_ERROR_EXISTS;
  memcpy(fs->nodes[node].name, new_name, length + 1);
  return 0;
}

static bool is_ancestor(struct tmpfs *fs, int32_t ancestor, int32_t node) {
  for (int32_t current = node; current != TMPFS_ROOT; current = fs->nodes[current].parent)
    if (current == ancestor)
      return true;
  return ancestor == TMPFS_ROOT;
}

static int32_t tmpfs_move(void *context, const char *path, const char *destination) {
  struct tmpfs *fs = context;
  int32_t node = lookup(fs, path);
  int32_t directory = lookup(fs, destination);
  if (node < 0)
    return node;
  if (directory < 0)
    return directory;
  if (node == TMPFS_ROOT)
    return FS_ERROR_BUSY;
  if (!fs->nodes[directory].directory)
    return FS_ERROR_NOT_DIR;
  if (fs->nodes[node].directory && is_ancestor(fs, node, directory))
    return FS_ERROR_INVALID;
  uint32_t length = (uint32_t)strlen(fs->nodes[node].name);
  int32_t existing = find_child(fs, directory, fs->nodes[node].name, length);
  if (existing >= 0 && existing != node)
    return FS_ERROR_EXISTS;
  fs->nodes[node].parent = (int16_t)directory;
  return 0;
}

static int32_t obtain_file(struct tmpfs *fs, const char *path, int32_t *node) {
  int32_t found = lookup(fs, path);
  if (found == FS_ERROR_NOT_FOUND) {
    int32_t status = create_entry(fs, path, false);
    if (status < 0)
      return status;
    found = lookup(fs, path);
  }
  if (found < 0)
    return found;
  if (fs->nodes[found].directory)
    return FS_ERROR_NOT_FILE;
  *node = found;
  return 0;
}

static int32_t tmpfs_write_file(void *context, const char *path, const void *buffer,
                                uint32_t count) {
  struct tmpfs *fs = context;
  int32_t node;
  int32_t status = obtain_file(fs, path, &node);
  if (status < 0)
    return status;
  if (is_open(fs, node))
    return FS_ERROR_BUSY;
  status = reserve(fs, &fs->nodes[node], count);
  if (status < 0)
    return status;
  if (count)
    memcpy(fs->nodes[node].data, buffer, count);
  fs->nodes[node].size = count;
  return (int32_t)count;
}

static int32_t tmpfs_append_file(void *context, const char *path, const void *buffer,
                                 uint32_t count) {
  struct tmpfs *fs = context;
  int32_t node;
  int32_t status = obtain_file(fs, path, &node);
  if (status < 0)
    return status;
  if (fs->nodes[node].size + (uint64_t)count > TMPFS_MAX_FILE)
    return FS_ERROR_NO_SPACE;
  status = reserve(fs, &fs->nodes[node], fs->nodes[node].size + count);
  if (status < 0)
    return status;
  if (count)
    memcpy(fs->nodes[node].data + fs->nodes[node].size, buffer, count);
  fs->nodes[node].size += count;
  return (int32_t)count;
}

const struct vfs_fs_ops vfs_tmpfs_ops = {
    .name = "tmpfs",
    .open = tmpfs_open,
    .read = tmpfs_read,
    .seek = tmpfs_seek,
    .close = tmpfs_close,
    .stat = tmpfs_stat,
    .list = tmpfs_list,
    .list_long = tmpfs_list_long,
    .create_file = tmpfs_create_file,
    .create_directory = tmpfs_create_directory,
    .unlink = tmpfs_unlink,
    .rename = tmpfs_rename,
    .move = tmpfs_move,
    .write_file = tmpfs_write_file,
    .append_file = tmpfs_append_file,
};
