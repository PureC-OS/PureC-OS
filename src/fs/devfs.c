#include "vfs_backends.h"
#include "../drivers/storage/block_device.h"
#include "../drivers/storage/storage_types.h"
#include "../kernel/diagnostics/klog.h"
#include "../lib/string.h"

#define DEVFS_MAX_OPEN 32
#define DEVFS_MAX_BLOCK 16
#define DEVFS_SECTOR_MAX 4096
#define DEVFS_KMSG_WRITE_MAX 255

enum devfs_kind {
  DEVFS_NULL,
  DEVFS_ZERO,
  DEVFS_FULL,
  DEVFS_RANDOM,
  DEVFS_KMSG,
  DEVFS_BLOCK
};

struct devfs_node {
  const char *name;
  enum devfs_kind kind;
};

struct devfs_file {
  bool used;
  enum devfs_kind kind;
  char block_name[STORAGE_DEVICE_NAME_CAPACITY];
  uint64_t block_size;
  uint32_t sector_size;
  uint64_t position;
  uint64_t klog_cursor;
  bool klog_data_lost;
};

static const struct devfs_node char_nodes[] = {
    {"null", DEVFS_NULL},     {"zero", DEVFS_ZERO}, {"full", DEVFS_FULL},
    {"random", DEVFS_RANDOM}, {"urandom", DEVFS_RANDOM},
    {"kmsg", DEVFS_KMSG}};

#define CHAR_NODE_COUNT (sizeof(char_nodes) / sizeof(char_nodes[0]))

static struct devfs_file files[DEVFS_MAX_OPEN];
static uint8_t sector_buffer[DEVFS_SECTOR_MAX];
static uint64_t random_state;

static uint64_t read_tsc(void) {
  uint32_t low, high;
  __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
  return ((uint64_t)high << 32) | low;
}

static bool hardware_random(uint64_t *value) {
  uint32_t eax = 1, ebx, ecx = 0, edx;
  __asm__ volatile("cpuid"
                   : "+a"(eax), "=b"(ebx), "+c"(ecx), "=d"(edx));
  if (!(ecx & (1U << 30)))
    return false;
  for (int attempt = 0; attempt < 10; attempt++) {
    uint64_t result;
    unsigned char ok;
    __asm__ volatile("rdrand %0; setc %1" : "=r"(result), "=qm"(ok));
    if (ok) {
      *value = result;
      return true;
    }
  }
  return false;
}

static uint64_t random_next(void) {
  if (!random_state) {
    uint64_t seed = 0;
    (void)hardware_random(&seed);
    random_state = seed ^ read_tsc() ^ 0x9E3779B97F4A7C15ULL;
    if (!random_state)
      random_state = 0x9E3779B97F4A7C15ULL;
  }
  random_state ^= random_state >> 12;
  random_state ^= random_state << 25;
  random_state ^= random_state >> 27;
  return random_state * 0x2545F4914F6CDD1DULL;
}

static void random_mix(const void *data, uint32_t count) {
  const uint8_t *bytes = data;
  (void)random_next();
  for (uint32_t i = 0; i < count; i++)
    random_state = (random_state << 5 | random_state >> 59) ^ bytes[i];
  if (!random_state)
    random_state = 0x9E3779B97F4A7C15ULL;
}

static bool block_info_for(const char *name, struct storage_device_info *info) {
  int32_t index = block_device_find(name);
  return index >= 0 && block_device_get_info((uint32_t)index, info);
}

static const struct devfs_node *find_char_node(const char *name) {
  for (uint32_t i = 0; i < CHAR_NODE_COUNT; i++)
    if (strcmp(name, char_nodes[i].name) == 0)
      return &char_nodes[i];
  return 0;
}

static int32_t lookup(const char *path, enum devfs_kind *kind,
                      struct storage_device_info *block) {
  if (!path || path[0] != '/' || !path[1])
    return FS_ERROR_NOT_FOUND;
  const struct devfs_node *node = find_char_node(path + 1);
  if (node) {
    *kind = node->kind;
    return 0;
  }
  for (const char *cursor = path + 1; *cursor; cursor++)
    if (*cursor == '/')
      return FS_ERROR_NOT_FOUND;
  if (!block_info_for(path + 1, block))
    return FS_ERROR_NOT_FOUND;
  *kind = DEVFS_BLOCK;
  return 0;
}

static struct devfs_file *get_file(int32_t descriptor) {
  if (descriptor < 0 || descriptor >= DEVFS_MAX_OPEN || !files[descriptor].used)
    return 0;
  return &files[descriptor];
}

static int32_t devfs_open(void *context, const char *path) {
  (void)context;
  enum devfs_kind kind;
  struct storage_device_info block;
  int32_t status = lookup(path, &kind, &block);
  if (status < 0)
    return status;
  for (int32_t i = 0; i < DEVFS_MAX_OPEN; i++) {
    if (files[i].used)
      continue;
    memset(&files[i], 0, sizeof(files[i]));
    files[i].used = true;
    files[i].kind = kind;
    if (kind == DEVFS_BLOCK) {
      strncpy(files[i].block_name, block.name, sizeof(files[i].block_name) - 1);
      files[i].sector_size = block.sector_size ? block.sector_size : 512;
      files[i].block_size = block.sector_count * files[i].sector_size;
    }
    if (kind == DEVFS_KMSG) {
      uint64_t total = klog_total_bytes();
      files[i].klog_cursor = total > 65536 ? total - 65536 : 0;
    }
    return i;
  }
  return FS_ERROR_NO_SPACE;
}

static int32_t devfs_close(void *context, int32_t descriptor) {
  (void)context;
  struct devfs_file *file = get_file(descriptor);
  if (!file)
    return FS_ERROR_INVALID;
  file->used = false;
  return 0;
}

static int32_t block_read(struct devfs_file *file, uint8_t *buffer,
                          uint32_t count) {
  if (file->sector_size > DEVFS_SECTOR_MAX)
    return FS_ERROR_UNSUPPORTED;
  if (file->position >= file->block_size)
    return 0;
  if (count > file->block_size - file->position)
    count = (uint32_t)(file->block_size - file->position);
  int32_t index = block_device_find(file->block_name);
  if (index < 0)
    return FS_ERROR_NOT_FOUND;
  char previous[STORAGE_DEVICE_NAME_CAPACITY];
  strncpy(previous, block_device_name(), sizeof(previous) - 1);
  previous[sizeof(previous) - 1] = '\0';
  if (!block_device_select((uint32_t)index))
    return FS_ERROR_IO;
  uint32_t done = 0;
  int32_t status = 0;
  while (done < count) {
    uint64_t lba = file->position / file->sector_size;
    uint32_t offset = (uint32_t)(file->position % file->sector_size);
    if (lba > 0xFFFFFFFFULL || !block_device_read((uint32_t)lba, sector_buffer)) {
      status = FS_ERROR_IO;
      break;
    }
    uint32_t chunk = file->sector_size - offset;
    if (chunk > count - done)
      chunk = count - done;
    memcpy(buffer + done, sector_buffer + offset, chunk);
    done += chunk;
    file->position += chunk;
  }
  int32_t restore = block_device_find(previous);
  if (restore >= 0 && restore != index)
    (void)block_device_select((uint32_t)restore);
  if (status < 0 && done == 0)
    return status;
  return (int32_t)done;
}

static int32_t devfs_read(void *context, int32_t descriptor, void *buffer,
                          uint32_t count) {
  (void)context;
  struct devfs_file *file = get_file(descriptor);
  if (!file)
    return FS_ERROR_INVALID;
  uint8_t *bytes = buffer;
  switch (file->kind) {
  case DEVFS_NULL:
    return 0;
  case DEVFS_ZERO:
  case DEVFS_FULL:
    memset(bytes, 0, count);
    return (int32_t)count;
  case DEVFS_RANDOM:
    for (uint32_t i = 0; i < count;) {
      uint64_t value = random_next();
      for (uint32_t j = 0; j < 8 && i < count; j++, i++)
        bytes[i] = (uint8_t)(value >> (j * 8));
    }
    return (int32_t)count;
  case DEVFS_KMSG:
    return (int32_t)klog_read_since(&file->klog_cursor, (char *)buffer, count,
                                    &file->klog_data_lost);
  case DEVFS_BLOCK:
    return block_read(file, bytes, count);
  }
  return FS_ERROR_INVALID;
}

static int64_t devfs_seek(void *context, int32_t descriptor, int64_t offset,
                          uint32_t whence) {
  (void)context;
  struct devfs_file *file = get_file(descriptor);
  if (!file)
    return FS_ERROR_INVALID;
  if (file->kind == DEVFS_KMSG) {
    uint64_t total = klog_total_bytes();
    int64_t base = whence == SEEK_CUR   ? (int64_t)file->klog_cursor
                   : whence == SEEK_END ? (int64_t)total
                                        : 0;
    int64_t target = base + offset;
    if (target < 0)
      target = 0;
    if ((uint64_t)target > total)
      target = (int64_t)total;
    file->klog_cursor = (uint64_t)target;
    return target;
  }
  if (file->kind != DEVFS_BLOCK)
    return 0;
  int64_t base = whence == SEEK_CUR   ? (int64_t)file->position
                 : whence == SEEK_END ? (int64_t)file->block_size
                                      : 0;
  int64_t target = base + offset;
  if (target < 0)
    return FS_ERROR_INVALID;
  file->position = (uint64_t)target;
  return target;
}

static int32_t devfs_stat(void *context, const char *path,
                          struct file_stat_info *out) {
  (void)context;
  if (strcmp(path, "/") == 0) {
    out->size = 0;
    out->is_directory = 1;
    out->reserved = FS_NODE_FILE;
    return 0;
  }
  enum devfs_kind kind;
  struct storage_device_info block;
  int32_t status = lookup(path, &kind, &block);
  if (status < 0)
    return status;
  out->is_directory = 0;
  if (kind == DEVFS_BLOCK) {
    out->size = block.sector_count * (block.sector_size ? block.sector_size : 512);
    out->reserved = FS_NODE_BLOCK_DEVICE;
  } else {
    out->size = 0;
    out->reserved = FS_NODE_CHAR_DEVICE;
  }
  return 0;
}

static uint32_t block_names(struct storage_device_info *devices) {
  int32_t count = block_device_list(devices, DEVFS_MAX_BLOCK);
  return count > 0 ? (uint32_t)count : 0;
}

static int32_t devfs_list(void *context, const char *path,
                          struct fs_directory_entry *entries,
                          uint32_t capacity) {
  (void)context;
  if (strcmp(path, "/") != 0)
    return FS_ERROR_NOT_DIR;
  uint32_t count = 0;
  for (uint32_t i = 0; i < CHAR_NODE_COUNT && count < capacity; i++, count++) {
    strncpy(entries[count].name, char_nodes[i].name,
            FS_DIRECTORY_NAME_CAPACITY - 1);
    entries[count].name[FS_DIRECTORY_NAME_CAPACITY - 1] = '\0';
    entries[count].size = 0;
    entries[count].attributes = FS_ATTRIBUTE_CHAR_DEVICE;
  }
  struct storage_device_info devices[DEVFS_MAX_BLOCK];
  uint32_t blocks = block_names(devices);
  for (uint32_t i = 0; i < blocks && count < capacity; i++, count++) {
    strncpy(entries[count].name, devices[i].name,
            FS_DIRECTORY_NAME_CAPACITY - 1);
    entries[count].name[FS_DIRECTORY_NAME_CAPACITY - 1] = '\0';
    entries[count].size = (uint32_t)(devices[i].sector_count *
                                     (devices[i].sector_size ? devices[i].sector_size : 512));
    entries[count].attributes = FS_ATTRIBUTE_BLOCK_DEVICE;
  }
  return (int32_t)count;
}

static int32_t devfs_list_long(void *context, const char *path,
                               struct fs_directory_entry_long *entries,
                               uint32_t capacity) {
  (void)context;
  if (strcmp(path, "/") != 0)
    return FS_ERROR_NOT_DIR;
  uint32_t count = 0;
  for (uint32_t i = 0; i < CHAR_NODE_COUNT && count < capacity; i++, count++) {
    strncpy(entries[count].name, char_nodes[i].name, FS_LONG_NAME_CAPACITY - 1);
    entries[count].name[FS_LONG_NAME_CAPACITY - 1] = '\0';
    entries[count].size = 0;
    entries[count].attributes = FS_ATTRIBUTE_CHAR_DEVICE;
  }
  struct storage_device_info devices[DEVFS_MAX_BLOCK];
  uint32_t blocks = block_names(devices);
  for (uint32_t i = 0; i < blocks && count < capacity; i++, count++) {
    strncpy(entries[count].name, devices[i].name, FS_LONG_NAME_CAPACITY - 1);
    entries[count].name[FS_LONG_NAME_CAPACITY - 1] = '\0';
    entries[count].size = (uint32_t)(devices[i].sector_count *
                                     (devices[i].sector_size ? devices[i].sector_size : 512));
    entries[count].attributes = FS_ATTRIBUTE_BLOCK_DEVICE;
  }
  return (int32_t)count;
}

static int32_t devfs_write_file(void *context, const char *path,
                                const void *buffer, uint32_t count) {
  (void)context;
  enum devfs_kind kind;
  struct storage_device_info block;
  int32_t status = lookup(path, &kind, &block);
  if (status < 0)
    return status;
  switch (kind) {
  case DEVFS_NULL:
  case DEVFS_ZERO:
    return (int32_t)count;
  case DEVFS_FULL:
    return FS_ERROR_NO_SPACE;
  case DEVFS_RANDOM:
    random_mix(buffer, count);
    return (int32_t)count;
  case DEVFS_KMSG: {
    char message[DEVFS_KMSG_WRITE_MAX + 1];
    uint32_t length = count < DEVFS_KMSG_WRITE_MAX ? count : DEVFS_KMSG_WRITE_MAX;
    memcpy(message, buffer, length);
    while (length && (message[length - 1] == '\n' || message[length - 1] == '\r'))
      length--;
    message[length] = '\0';
    if (length)
      klog(KLOG_INFO, message);
    return (int32_t)count;
  }
  case DEVFS_BLOCK:
    return FS_ERROR_READ_ONLY;
  }
  return FS_ERROR_INVALID;
}

const struct vfs_fs_ops vfs_devfs_ops = {
    .name = "devfs",
    .open = devfs_open,
    .read = devfs_read,
    .seek = devfs_seek,
    .close = devfs_close,
    .stat = devfs_stat,
    .list = devfs_list,
    .list_long = devfs_list_long,
    .write_file = devfs_write_file,
    .append_file = devfs_write_file,
};
