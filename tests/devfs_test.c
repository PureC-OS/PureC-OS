#include "../src/drivers/storage/block_device.h"
#include "../src/drivers/storage/storage_types.h"
#include "../src/fs/vfs_backends.h"
#include "../src/kernel/diagnostics/klog.h"
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #condition);               \
      failures++;                                                              \
    }                                                                          \
  } while (0)

#define SECTOR 512
#define SECTORS 4

static uint8_t disk[SECTORS * SECTOR];
static int selected = 0;
static char last_klog[256];
static char log_text[] = "boot line one\nboot line two\n";

int32_t block_device_list(struct storage_device_info *devices,
                          uint32_t capacity) {
  if (capacity < 1)
    return 0;
  memset(&devices[0], 0, sizeof(devices[0]));
  strcpy(devices[0].name, "sda");
  devices[0].sector_count = SECTORS;
  devices[0].sector_size = SECTOR;
  return 1;
}
bool block_device_get_info(uint32_t index, struct storage_device_info *info) {
  return index == 0 && block_device_list(info, 1) == 1;
}
int32_t block_device_find(const char *name) {
  return strcmp(name, "sda") == 0 ? 0 : -1;
}
bool block_device_select(uint32_t index) {
  selected = (int)index + 1;
  return index == 0;
}
const char *block_device_name(void) { return "sda"; }
bool block_device_read(uint32_t lba, void *buffer) {
  if (lba >= SECTORS)
    return false;
  memcpy(buffer, disk + lba * SECTOR, SECTOR);
  return true;
}
void klog(enum klog_level level, const char *message) {
  (void)level;
  strncpy(last_klog, message, sizeof(last_klog) - 1);
}
#include "../src/kernel/random/krandom.h"
static uint64_t test_rng_state = 0x1234567;
void krandom_fill(void *buffer, uint32_t count) {
  uint8_t *bytes = buffer;
  for (uint32_t i = 0; i < count; i++) {
    test_rng_state = test_rng_state * 6364136223846793005ULL + 1442695040888963407ULL;
    bytes[i] = (uint8_t)(test_rng_state >> 56);
  }
}
void krandom_mix(const void *data, uint32_t count) { (void)data; (void)count; }
uint64_t klog_total_bytes(void) { return strlen(log_text); }
uint32_t klog_read_since(uint64_t *cursor, char *buffer, uint32_t capacity,
                         bool *data_lost) {
  *data_lost = false;
  uint64_t total = strlen(log_text);
  uint32_t count = total - *cursor < capacity ? (uint32_t)(total - *cursor) : capacity;
  memcpy(buffer, log_text + *cursor, count);
  *cursor += count;
  return count;
}

static const struct vfs_fs_ops *ops = &vfs_devfs_ops;

static int32_t open_device(const char *path) { return ops->open(0, path); }

int main(void) {
  for (uint32_t i = 0; i < sizeof(disk); i++)
    disk[i] = (uint8_t)(i * 7 + 3);

  uint8_t buffer[1024];
  int32_t fd = open_device("/null");
  CHECK(fd >= 0);
  CHECK(ops->read(0, fd, buffer, sizeof(buffer)) == 0);
  CHECK(ops->close(0, fd) == 0);
  CHECK(ops->close(0, fd) < 0);
  CHECK(ops->write_file(0, "/null", "abc", 3) == 3);

  fd = open_device("/zero");
  memset(buffer, 0xAA, sizeof(buffer));
  CHECK(ops->read(0, fd, buffer, 100) == 100);
  int zeros = 1;
  for (int i = 0; i < 100; i++)
    zeros &= buffer[i] == 0;
  CHECK(zeros);
  CHECK(buffer[100] == 0xAA);
  ops->close(0, fd);

  CHECK(ops->write_file(0, "/full", "x", 1) == FS_ERROR_NO_SPACE);

  fd = open_device("/random");
  uint8_t first[64], second[64];
  CHECK(ops->read(0, fd, first, 64) == 64);
  CHECK(ops->read(0, fd, second, 64) == 64);
  CHECK(memcmp(first, second, 64) != 0);
  int distinct = 0;
  for (int i = 1; i < 64; i++)
    distinct += first[i] != first[0];
  CHECK(distinct > 32);
  ops->close(0, fd);
  CHECK(ops->write_file(0, "/urandom", "seed", 4) == 4);

  fd = open_device("/kmsg");
  char text[128];
  int32_t got = ops->read(0, fd, text, sizeof(text) - 1);
  CHECK(got == (int32_t)strlen(log_text));
  CHECK(ops->seek(0, fd, 0, SEEK_SET) == 0);
  CHECK(ops->read(0, fd, text, 4) == 4);
  CHECK(memcmp(text, "boot", 4) == 0);
  ops->close(0, fd);
  CHECK(ops->write_file(0, "/kmsg", "hello\n", 6) == 6);
  CHECK(strcmp(last_klog, "hello") == 0);

  fd = open_device("/sda");
  CHECK(fd >= 0);
  CHECK(ops->seek(0, fd, SECTOR - 4, SEEK_SET) == SECTOR - 4);
  CHECK(ops->read(0, fd, buffer, 8) == 8);
  CHECK(memcmp(buffer, disk + SECTOR - 4, 8) == 0);
  CHECK(ops->seek(0, fd, -2, SEEK_END) == SECTORS * SECTOR - 2);
  CHECK(ops->read(0, fd, buffer, 16) == 2);
  CHECK(ops->read(0, fd, buffer, 16) == 0);
  CHECK(ops->seek(0, fd, 0, SEEK_SET) == 0);
  CHECK(ops->read(0, fd, buffer, 1024) == 1024);
  CHECK(memcmp(buffer, disk, 1024) == 0);
  CHECK(selected == 1);
  ops->close(0, fd);
  CHECK(ops->write_file(0, "/sda", "x", 1) == FS_ERROR_READ_ONLY);

  struct file_stat_info info;
  CHECK(ops->stat(0, "/", &info) == 0 && info.is_directory);
  CHECK(ops->stat(0, "/null", &info) == 0 && info.reserved == FS_NODE_CHAR_DEVICE);
  CHECK(ops->stat(0, "/sda", &info) == 0 && info.reserved == FS_NODE_BLOCK_DEVICE &&
        info.size == SECTORS * SECTOR);
  CHECK(ops->stat(0, "/missing", &info) == FS_ERROR_NOT_FOUND);
  CHECK(open_device("/missing") == FS_ERROR_NOT_FOUND);
  CHECK(open_device("/null/x") == FS_ERROR_NOT_FOUND);

  struct fs_directory_entry_long entries[16];
  int32_t count = ops->list_long(0, "/", entries, 16);
  CHECK(count == 7);
  CHECK(strcmp(entries[0].name, "null") == 0);
  CHECK(entries[0].attributes == FS_ATTRIBUTE_CHAR_DEVICE);
  CHECK(strcmp(entries[6].name, "sda") == 0);
  CHECK(entries[6].attributes == FS_ATTRIBUTE_BLOCK_DEVICE);
  CHECK(ops->list_long(0, "/null", entries, 16) == FS_ERROR_NOT_DIR);

  int32_t many[40];
  int opened = 0;
  for (int i = 0; i < 40; i++) {
    many[i] = open_device("/null");
    opened += many[i] >= 0;
  }
  CHECK(opened == 32);
  CHECK(many[32] == FS_ERROR_NO_SPACE);
  for (int i = 0; i < 32; i++)
    ops->close(0, many[i]);

  if (failures) {
    printf("devfs_test: %d failure(s)\n", failures);
    return 1;
  }
  printf("devfs_test: ok\n");
  return 0;
}
