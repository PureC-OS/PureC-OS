#include "../src/drivers/storage/block_device.h"
#include "../src/fs/ext2/include/ext2.h"
#include "../src/fs/ext2/include/ext2_block.h"
#include "../src/fs/ext2/include/ext2_file.h"
#include "../src/fs/ext2/include/ext2_format.h"
#include "../src/fs/ext2/include/ext2_inode.h"
#include "../src/fs/ext2/include/ext2_meta.h"
#include "../src/fs/ext2/include/ext2_dir.h"
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


static int32_t make_file(const char *path, const char *text) {
  if (ext2_create_file(path) < 0)
    return -1;
  return ext2_write_file(path, text, (uint32_t)strlen(text));
}

static bool exists(const char *path) {
  uint32_t ino;
  return ext2_dir_resolve(path, &ino) == 0;
}

int main(void) {
  disk = calloc(DISK_SECTORS, 512);
  CHECK(ext2_format_at(0, DISK_SECTORS) == 0);
  CHECK(ext2_super_mount_at(0));
  uint32_t free_blocks_before = 0;
  {
    uint8_t descriptor[32];
    ext2_group_desc_read(0, descriptor);
    free_blocks_before = ext2_read_u16(descriptor + 12);
  }

  CHECK(ext2_create_directory("/home") == 0);
  CHECK(ext2_create_directory("/home/ann") == 0);
  CHECK(make_file("/home/ann/a.txt", "alpha") == 5);
  CHECK(make_file("/home/ann/b.txt", "bravo") == 5);
  CHECK(make_file("/home/ann/c.txt", "charlie") == 7);

  struct ext2_meta meta;
  CHECK(ext2_meta_get("/home/ann/a.txt", &meta) == 0);
  CHECK(!meta.directory && meta.mode == 0644 && meta.uid == 0);
  CHECK(ext2_meta_set("/home/ann", 0700, 1000, 1000) == 0);
  CHECK(ext2_meta_set("/home/ann/a.txt", 0600, 70000, 100000) == 0);
  CHECK(ext2_meta_get("/home/ann", &meta) == 0);
  CHECK(meta.directory && meta.mode == 0700 && meta.uid == 1000 &&
        meta.gid == 1000);
  CHECK(ext2_meta_get("/home/ann/a.txt", &meta) == 0);
  CHECK(!meta.directory && meta.mode == 0600 && meta.uid == 70000 &&
        meta.gid == 100000);
  CHECK(ext2_meta_set("/home/ann/a.txt", -1, EXT2_META_KEEP, 5) == 0);
  CHECK(ext2_meta_get("/home/ann/a.txt", &meta) == 0);
  CHECK(meta.mode == 0600 && meta.uid == 70000 && meta.gid == 5);
  CHECK(ext2_meta_get("/nope", &meta) < 0);

  CHECK(ext2_rename("/home/ann/b.txt", "bb.txt") == 0);
  CHECK(!exists("/home/ann/b.txt") && exists("/home/ann/bb.txt"));
  CHECK(ext2_rename("/home/ann/bb.txt", "c.txt") == FS_ERROR_EXISTS);
  CHECK(ext2_rename("/home/ann/bb.txt", "x/y") < 0);

  int32_t handle = ext2_open("/home/ann/bb.txt");
  CHECK(handle >= 0);
  char text[16] = {0};
  CHECK(ext2_read(handle, text, 15) == 5 && memcmp(text, "bravo", 5) == 0);
  ext2_close(handle);

  CHECK(ext2_create_directory("/srv") == 0);
  CHECK(ext2_move("/home/ann/c.txt", "/srv") == 0);
  CHECK(!exists("/home/ann/c.txt") && exists("/srv/c.txt"));
  CHECK(ext2_move("/srv/c.txt", "/srv/c.txt") < 0);
  CHECK(ext2_move("/home/ann", "/home/ann") < 0);

  CHECK(ext2_create_directory("/srv/sub") == 0);
  CHECK(make_file("/srv/sub/deep.txt", "deep") == 4);
  CHECK(ext2_move("/srv/sub", "/home") == 0);
  CHECK(exists("/home/sub/deep.txt") && !exists("/srv/sub"));
  uint32_t moved_ino, home_ino, dotdot;
  CHECK(ext2_dir_resolve("/home/sub", &moved_ino) == 0);
  CHECK(ext2_dir_resolve("/home", &home_ino) == 0);
  CHECK(ext2_dir_find(moved_ino, "..", &dotdot, 0) == 0 && dotdot == home_ino);
  CHECK(ext2_move("/home", "/home/sub") < 0);

  CHECK(ext2_delete("/home/sub") < 0);
  CHECK(exists("/home/sub/deep.txt"));
  CHECK(ext2_delete("/home/sub/deep.txt") == 0);
  CHECK(!exists("/home/sub/deep.txt"));
  CHECK(ext2_delete("/home/sub") == 0);
  CHECK(!exists("/home/sub"));
  CHECK(ext2_delete("/") < 0);
  CHECK(ext2_delete("/missing") < 0);

  CHECK(ext2_delete("/home/ann/a.txt") == 0);
  CHECK(ext2_delete("/home/ann/bb.txt") == 0);
  CHECK(ext2_delete("/srv/c.txt") == 0);
  CHECK(ext2_delete("/home/ann") == 0);
  CHECK(ext2_delete("/srv") == 0);
  CHECK(ext2_delete("/home") == 0);

  CHECK(make_file("/again.txt", "reuse") == 5);
  CHECK(ext2_delete("/again.txt") == 0);

  uint8_t descriptor[32];
  ext2_group_desc_read(0, descriptor);
  uint32_t free_blocks_after = ext2_read_u16(descriptor + 12);
  CHECK(free_blocks_after == free_blocks_before);

  if (failures || warnings) {
    printf("ext2_meta_test: %d failures %d warnings\n", failures, warnings);
    return 1;
  }
  printf("ext2_meta_test: ok\n");
  return 0;
}
