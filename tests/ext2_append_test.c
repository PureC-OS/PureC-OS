#include "../src/drivers/storage/block_device.h"
#include "../src/fs/ext2/include/ext2.h"
#include "../src/fs/ext2/include/ext2_block.h"
#include "../src/fs/ext2/include/ext2_file.h"
#include "../src/fs/ext2/include/ext2_format.h"
#include "../src/fs/ext2/include/ext2_inode.h"
#include "../src/fs/ext2/include/ext2_super.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DISK_SECTORS (64u * 1024 * 1024 / 512)
#define TOTAL_LIMIT (40u * 1024 * 1024)

static uint8_t *disk;
static int failures;
static int warnings;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #condition);               \
      failures++;                                                              \
    }                                                                          \
  } while (0)

void klog(int level, const char *message) {
  (void)level;
  if (strstr(message, "reserved") || strstr(message, "already free"))
    warnings++;
}

void klogf(int level, const char *format, ...) {
  char text[512];
  va_list args;
  va_start(args, format);
  vsnprintf(text, sizeof(text), format, args);
  va_end(args);
  if (level >= 2 || strstr(text, "reserved") || strstr(text, "already free")) {
    warnings++;
    printf("LOG: %s\n", text);
  }
}

bool block_device_init(void) { return true; }
uint32_t block_device_count(void) { return 1; }
bool block_device_select(uint32_t index) { return index == 0; }
const char *block_device_name(void) { return "sda"; }
bool block_device_flush(void) { return true; }
bool block_device_read(uint32_t lba, void *buffer) {
  if (lba >= DISK_SECTORS)
    return false;
  memcpy(buffer, disk + (size_t)lba * 512, 512);
  return true;
}
bool block_device_write(uint32_t lba, const void *buffer) {
  if (lba >= DISK_SECTORS)
    return false;
  memcpy(disk + (size_t)lba * 512, buffer, 512);
  return true;
}
bool block_device_read_sectors(uint32_t lba, void *buffer, uint32_t count) {
  for (uint32_t i = 0; i < count; i++)
    if (!block_device_read(lba + i, (uint8_t *)buffer + i * 512))
      return false;
  return true;
}
bool block_device_write_sectors(uint32_t lba, const void *buffer,
                                uint32_t count) {
  for (uint32_t i = 0; i < count; i++)
    if (!block_device_write(lba + i, (const uint8_t *)buffer + i * 512))
      return false;
  return true;
}
int32_t block_device_find(const char *name) {
  (void)name;
  return 0;
}
bool block_device_get_info(uint32_t index, struct storage_device_info *info) {
  (void)index;
  memset(info, 0, sizeof(*info));
  strcpy(info->name, "sda");
  info->sector_size = 512;
  info->sector_count = DISK_SECTORS;
  info->operational = 1;
  info->writable = 1;
  return true;
}
uint64_t pmm_allocate_contiguous(uint64_t pages) {
  return (uint64_t)(uintptr_t)calloc(pages, 4096);
}
void pmm_free_contiguous(uint64_t address, uint64_t pages) {
  (void)pages;
  free((void *)(uintptr_t)address);
}
void *pmm_physical_to_virtual(uint64_t address) {
  return (void *)(uintptr_t)address;
}

extern int ext2_dir_resolve(const char *path, uint32_t *inode);

static uint32_t group_free_blocks(void) {
  uint8_t descriptor[32];
  ext2_group_desc_read(0, descriptor);
  return ext2_read_u16(descriptor + 12);
}

int main(void) {
  disk = calloc(DISK_SECTORS, 512);
  CHECK(ext2_format_at(0, DISK_SECTORS) == 0);
  CHECK(ext2_super_mount_at(0));
  uint32_t free_before = group_free_blocks();

  const char *path = "/kernel.log";
  uint8_t *reference = malloc(TOTAL_LIMIT);
  uint32_t total = 0;
  srand(7);
  for (int round = 0; round < 400; round++) {
    uint32_t count = 1 + (uint32_t)rand() % 90000;
    if (total + count > TOTAL_LIMIT)
      break;
    for (uint32_t i = 0; i < count; i++)
      reference[total + i] = (uint8_t)rand();
    int32_t result = ext2_append_file(path, reference + total, count);
    CHECK(result == (int32_t)count);
    if (result != (int32_t)count)
      return 1;
    total += count;
  }

  uint32_t inode;
  uint8_t record[256];
  CHECK(ext2_dir_resolve(path, &inode) >= 0);
  CHECK(ext2_inode_read(inode, record));
  CHECK(ext2_read_u32(record + 4) == total);
  CHECK(total > (12u + 256u + 256u * 256u) / 4 * 1024);

  int32_t handle = ext2_open(path);
  uint8_t *readback = malloc(total);
  uint32_t got = 0;
  while (got < total) {
    int32_t chunk = ext2_read(handle, readback + got,
                              total - got > 65536 ? 65536 : total - got);
    if (chunk <= 0)
      break;
    got += (uint32_t)chunk;
  }
  CHECK(got == total);
  CHECK(memcmp(readback, reference, total) == 0);

  CHECK(ext2_write_file(path, "x", 1) == 1);
  CHECK(group_free_blocks() == free_before);
  CHECK(warnings == 0);

  if (failures) {
    printf("ext2_append_test: %d failure(s)\n", failures);
    return 1;
  }
  printf("ext2_append_test: ok\n");
  return 0;
}
