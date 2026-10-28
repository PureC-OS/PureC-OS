#include "vfs_backends.h"
#include "vfs.h"
#include "../drivers/pci/pci.h"
#include "../drivers/storage/block_device.h"
#include "../drivers/storage/storage_types.h"
#include "../kernel/smp/cpu.h"
#include "../lib/format.h"
#include "../lib/string.h"

#define SYSFS_MAX_OPEN 16
#define SYSFS_BUFFER 512
#define SYSFS_MAX_NODES 512
#define SYSFS_PATH_CAPACITY 64
#define SYSFS_MAX_PCI 48
#define SYSFS_MAX_BLOCK 16

enum node_kind {
  KIND_DIR,
  KIND_KERNEL_VERSION,
  KIND_CPU_ONLINE,
  KIND_CPU_APICID,
  KIND_PCI_VENDOR,
  KIND_PCI_DEVICE,
  KIND_PCI_CLASS,
  KIND_PCI_REVISION,
  KIND_BLOCK_SIZE,
  KIND_BLOCK_SECTOR_SIZE,
  KIND_BLOCK_MODEL,
  KIND_BLOCK_RO
};

struct node {
  char path[SYSFS_PATH_CAPACITY];
  uint8_t kind;
  uint8_t index;
};

struct sysfs_file {
  bool used;
  uint32_t length;
  uint32_t position;
  char data[SYSFS_BUFFER];
};

static struct sysfs_file files[SYSFS_MAX_OPEN];
static struct node nodes[SYSFS_MAX_NODES];
static uint32_t node_count;
static struct pci_device_info pci_devices[SYSFS_MAX_PCI];
static uint32_t pci_count;
static bool pci_scanned;
static struct storage_device_info block_devices[SYSFS_MAX_BLOCK];
static int32_t block_count;

static void collect_pci(const struct pci_device_info *device, void *context) {
  (void)context;
  if (pci_count < SYSFS_MAX_PCI)
    pci_devices[pci_count++] = *device;
}

static void add_node(const char *path, uint8_t kind, uint8_t index) {
  if (node_count >= SYSFS_MAX_NODES)
    return;
  struct node *node = &nodes[node_count++];
  strncpy(node->path, path, SYSFS_PATH_CAPACITY - 1);
  node->path[SYSFS_PATH_CAPACITY - 1] = '\0';
  node->kind = kind;
  node->index = index;
}

static void add_child(const char *directory, const char *name, uint8_t kind,
                      uint8_t index) {
  char path[SYSFS_PATH_CAPACITY];
  kformat(path, sizeof(path), "%s/%s", directory, name);
  add_node(path, kind, index);
}

static void build_tree(void) {
  node_count = 0;
  if (!pci_scanned) {
    pci_scanned = true;
    pci_enumerate(collect_pci, 0);
  }
  add_node("/", KIND_DIR, 0);
  add_node("/kernel", KIND_DIR, 0);
  add_node("/kernel/version", KIND_KERNEL_VERSION, 0);
  add_node("/devices", KIND_DIR, 0);
  add_node("/devices/system", KIND_DIR, 0);
  add_node("/devices/system/cpu", KIND_DIR, 0);
  uint32_t cpus = cpu_registered_count();
  for (uint32_t id = 0; id < cpus; id++) {
    char name[16], directory[SYSFS_PATH_CAPACITY];
    kformat(name, sizeof(name), "cpu%u", id);
    kformat(directory, sizeof(directory), "/devices/system/cpu/%s", name);
    add_node(directory, KIND_DIR, 0);
    add_child(directory, "online", KIND_CPU_ONLINE, (uint8_t)id);
    add_child(directory, "apicid", KIND_CPU_APICID, (uint8_t)id);
  }
  add_node("/bus", KIND_DIR, 0);
  add_node("/bus/pci", KIND_DIR, 0);
  add_node("/bus/pci/devices", KIND_DIR, 0);
  for (uint32_t i = 0; i < pci_count; i++) {
    char name[16], directory[SYSFS_PATH_CAPACITY];
    kformat(name, sizeof(name), "%02x:%02x.%u", pci_devices[i].bus,
            pci_devices[i].slot, pci_devices[i].function);
    kformat(directory, sizeof(directory), "/bus/pci/devices/%s", name);
    add_node(directory, KIND_DIR, 0);
    add_child(directory, "vendor", KIND_PCI_VENDOR, (uint8_t)i);
    add_child(directory, "device", KIND_PCI_DEVICE, (uint8_t)i);
    add_child(directory, "class", KIND_PCI_CLASS, (uint8_t)i);
    add_child(directory, "revision", KIND_PCI_REVISION, (uint8_t)i);
  }
  add_node("/block", KIND_DIR, 0);
  block_count = block_device_list(block_devices, SYSFS_MAX_BLOCK);
  if (block_count < 0)
    block_count = 0;
  for (int32_t i = 0; i < block_count; i++) {
    char directory[SYSFS_PATH_CAPACITY];
    kformat(directory, sizeof(directory), "/block/%s", block_devices[i].name);
    add_node(directory, KIND_DIR, 0);
    add_child(directory, "size", KIND_BLOCK_SIZE, (uint8_t)i);
    add_child(directory, "sector_size", KIND_BLOCK_SECTOR_SIZE, (uint8_t)i);
    add_child(directory, "model", KIND_BLOCK_MODEL, (uint8_t)i);
    add_child(directory, "ro", KIND_BLOCK_RO, (uint8_t)i);
  }
}

static const struct node *find_node(const char *path) {
  if (!path || path[0] != '/')
    return 0;
  build_tree();
  char normalized[SYSFS_PATH_CAPACITY];
  strncpy(normalized, path, sizeof(normalized) - 1);
  normalized[sizeof(normalized) - 1] = '\0';
  uint32_t length = strlen(normalized);
  while (length > 1 && normalized[length - 1] == '/')
    normalized[--length] = '\0';
  for (uint32_t i = 0; i < node_count; i++)
    if (strcmp(nodes[i].path, normalized) == 0)
      return &nodes[i];
  return 0;
}

static void generate(const struct node *node, char *out, uint32_t capacity) {
  uint32_t index = node->index;
  out[0] = '\0';
  switch (node->kind) {
  case KIND_KERNEL_VERSION:
    kformat(out, capacity, "PureC OS kernel 1.1.0 (x86_64)\n");
    break;
  case KIND_CPU_ONLINE:
    kformat(out, capacity, "%d\n", cpu_is_online(index) ? 1 : 0);
    break;
  case KIND_CPU_APICID: {
    const struct cpu_info *info = cpu_get_info(index);
    kformat(out, capacity, "%u\n", info ? info->lapic_id : 0);
    break;
  }
  case KIND_PCI_VENDOR:
    kformat(out, capacity, "0x%04x\n", pci_devices[index].vendor_id);
    break;
  case KIND_PCI_DEVICE:
    kformat(out, capacity, "0x%04x\n", pci_devices[index].device_id);
    break;
  case KIND_PCI_CLASS:
    kformat(out, capacity, "0x%02x%02x%02x\n", pci_devices[index].class_code,
            pci_devices[index].subclass,
            pci_devices[index].programming_interface);
    break;
  case KIND_PCI_REVISION:
    kformat(out, capacity, "0x%02x\n", pci_devices[index].revision);
    break;
  case KIND_BLOCK_SIZE:
    kformat(out, capacity, "%llu\n", block_devices[index].sector_count);
    break;
  case KIND_BLOCK_SECTOR_SIZE:
    kformat(out, capacity, "%u\n", block_devices[index].sector_size);
    break;
  case KIND_BLOCK_MODEL:
    kformat(out, capacity, "%s\n", block_devices[index].model);
    break;
  case KIND_BLOCK_RO:
    kformat(out, capacity, "%d\n", block_devices[index].writable ? 0 : 1);
    break;
  default:
    break;
  }
}

static int32_t sysfs_open(void *context, const char *path) {
  (void)context;
  const struct node *node = find_node(path);
  if (!node)
    return FS_ERROR_NOT_FOUND;
  if (node->kind == KIND_DIR)
    return FS_ERROR_NOT_FILE;
  for (int32_t i = 0; i < SYSFS_MAX_OPEN; i++) {
    if (files[i].used)
      continue;
    generate(node, files[i].data, SYSFS_BUFFER);
    files[i].used = true;
    files[i].length = strlen(files[i].data);
    files[i].position = 0;
    return i;
  }
  return FS_ERROR_NO_SPACE;
}

static struct sysfs_file *get_file(int32_t descriptor) {
  if (descriptor < 0 || descriptor >= SYSFS_MAX_OPEN || !files[descriptor].used)
    return 0;
  return &files[descriptor];
}

static int32_t sysfs_read(void *context, int32_t descriptor, void *buffer,
                          uint32_t count) {
  (void)context;
  struct sysfs_file *file = get_file(descriptor);
  if (!file)
    return FS_ERROR_INVALID;
  if (file->position >= file->length)
    return 0;
  uint32_t remaining = file->length - file->position;
  uint32_t amount = count < remaining ? count : remaining;
  memcpy(buffer, file->data + file->position, amount);
  file->position += amount;
  return (int32_t)amount;
}

static int64_t sysfs_seek(void *context, int32_t descriptor, int64_t offset,
                          uint32_t whence) {
  (void)context;
  struct sysfs_file *file = get_file(descriptor);
  if (!file)
    return FS_ERROR_INVALID;
  int64_t base = whence == SEEK_CUR   ? (int64_t)file->position
                 : whence == SEEK_END ? (int64_t)file->length
                                      : 0;
  int64_t target = base + offset;
  if (target < 0)
    return FS_ERROR_INVALID;
  if (target > (int64_t)file->length)
    target = file->length;
  file->position = (uint32_t)target;
  return target;
}

static int32_t sysfs_close(void *context, int32_t descriptor) {
  (void)context;
  struct sysfs_file *file = get_file(descriptor);
  if (!file)
    return FS_ERROR_INVALID;
  file->used = false;
  return 0;
}

static int32_t sysfs_stat(void *context, const char *path,
                          struct file_stat_info *out) {
  (void)context;
  const struct node *node = find_node(path);
  if (!node)
    return FS_ERROR_NOT_FOUND;
  out->size = 0;
  out->is_directory = node->kind == KIND_DIR;
  out->reserved = FS_NODE_FILE;
  return 0;
}

static bool child_name(const struct node *node, const char *directory,
                       uint32_t directory_length, const char **name) {
  const char *path = node->path;
  if (strncmp(path, directory, directory_length) != 0)
    return false;
  path += directory_length;
  if (directory_length > 1) {
    if (*path != '/')
      return false;
    path++;
  }
  if (!*path)
    return false;
  for (const char *c = path; *c; c++)
    if (*c == '/')
      return false;
  *name = path;
  return true;
}

static int32_t list_node(const char *path, void *entries, uint32_t capacity,
                         bool long_names) {
  const struct node *self = find_node(path);
  if (!self)
    return FS_ERROR_NOT_FOUND;
  if (self->kind != KIND_DIR)
    return FS_ERROR_NOT_DIR;
  const char *directory = self->path;
  uint32_t directory_length = strlen(directory);
  uint32_t count = 0;
  for (uint32_t i = 0; i < node_count && count < capacity; i++) {
    const char *name;
    if (!child_name(&nodes[i], directory, directory_length, &name))
      continue;
    uint8_t attributes = nodes[i].kind == KIND_DIR ? FS_ATTRIBUTE_DIRECTORY : 0;
    if (long_names) {
      struct fs_directory_entry_long *entry =
          &((struct fs_directory_entry_long *)entries)[count];
      strncpy(entry->name, name, FS_LONG_NAME_CAPACITY - 1);
      entry->name[FS_LONG_NAME_CAPACITY - 1] = '\0';
      entry->size = 0;
      entry->attributes = attributes;
    } else {
      struct fs_directory_entry *entry =
          &((struct fs_directory_entry *)entries)[count];
      strncpy(entry->name, name, FS_DIRECTORY_NAME_CAPACITY - 1);
      entry->name[FS_DIRECTORY_NAME_CAPACITY - 1] = '\0';
      entry->size = 0;
      entry->attributes = attributes;
    }
    count++;
  }
  return (int32_t)count;
}

static int32_t sysfs_list(void *context, const char *path,
                          struct fs_directory_entry *entries,
                          uint32_t capacity) {
  (void)context;
  return list_node(path, entries, capacity, false);
}

static int32_t sysfs_list_long(void *context, const char *path,
                               struct fs_directory_entry_long *entries,
                               uint32_t capacity) {
  (void)context;
  return list_node(path, entries, capacity, true);
}

const struct vfs_fs_ops vfs_sysfs_ops = {
    .name = "sysfs",
    .open = sysfs_open,
    .read = sysfs_read,
    .seek = sysfs_seek,
    .close = sysfs_close,
    .stat = sysfs_stat,
    .list = sysfs_list,
    .list_long = sysfs_list_long,
};
