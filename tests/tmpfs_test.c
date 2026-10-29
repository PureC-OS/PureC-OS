#include "../src/fs/vfs_backends.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #condition);               \
      failures++;                                                              \
    }                                                                          \
  } while (0)

static int allocations;
static uint32_t fake_clock = 1700000000;
uint32_t wallclock_unix_seconds(void) { return fake_clock; }
uint64_t pmm_total_bytes(void) { return 512ULL * 1024 * 1024; }
uint64_t pmm_allocate_contiguous(uint64_t pages) {
  void *memory = malloc((size_t)pages * 4096);
  if (memory)
    allocations++;
  return (uint64_t)(uintptr_t)memory;
}
void pmm_free_contiguous(uint64_t physical, uint64_t pages) {
  (void)pages;
  allocations--;
  free((void *)(uintptr_t)physical);
}
void *pmm_physical_to_virtual(uint64_t physical) { return (void *)(uintptr_t)physical; }

static const struct vfs_fs_ops *ops = &vfs_tmpfs_ops;

static int read_all(void *fs, const char *path, char *buffer, uint32_t capacity) {
  int32_t fd = ops->open(fs, path);
  if (fd < 0)
    return fd;
  int32_t n = ops->read(fs, fd, buffer, capacity - 1);
  ops->close(fs, fd);
  if (n >= 0)
    buffer[n] = '\0';
  return n;
}

int main(void) {
  void *fs = vfs_tmpfs_create();
  void *other = vfs_tmpfs_create();
  CHECK(fs && other && fs != other);
  char text[256];
  struct file_stat_info info;

  CHECK(ops->stat(fs, "/", &info) == 0 && info.is_directory);
  CHECK(ops->open(fs, "/nothing") == FS_ERROR_NOT_FOUND);
  CHECK(ops->create_file(fs, "/a.txt") == 0);
  CHECK(ops->create_file(fs, "/a.txt") == FS_ERROR_EXISTS);
  CHECK(ops->create_file(fs, "/missing/b.txt") == FS_ERROR_NOT_FOUND);
  CHECK(ops->create_file(fs, "/a.txt/b") == FS_ERROR_NOT_DIR);
  CHECK(ops->stat(other, "/a.txt", &info) == FS_ERROR_NOT_FOUND);

  CHECK(ops->write_file(fs, "/a.txt", "hello", 5) == 5);
  CHECK(read_all(fs, "/a.txt", text, sizeof(text)) == 5 && strcmp(text, "hello") == 0);
  CHECK(ops->append_file(fs, "/a.txt", " world", 6) == 6);
  CHECK(read_all(fs, "/a.txt", text, sizeof(text)) == 11 && strcmp(text, "hello world") == 0);
  CHECK(ops->write_file(fs, "/a.txt", "xy", 2) == 2);
  CHECK(read_all(fs, "/a.txt", text, sizeof(text)) == 2 && strcmp(text, "xy") == 0);
  CHECK(ops->write_file(fs, "/new.txt", "auto", 4) == 4);
  CHECK(ops->append_file(fs, "/appended.txt", "log", 3) == 3);
  CHECK(ops->stat(fs, "/appended.txt", &info) == 0 && info.size == 3 && !info.is_directory);

  CHECK(ops->create_directory(fs, "/dir") == 0);
  CHECK(ops->create_directory(fs, "/dir/sub") == 0);
  CHECK(ops->write_file(fs, "/dir/sub/f", "deep", 4) == 4);
  CHECK(ops->open(fs, "/dir") == FS_ERROR_NOT_FILE);
  CHECK(ops->write_file(fs, "/dir", "x", 1) == FS_ERROR_NOT_FILE);

  struct fs_directory_entry_long entries[16];
  int32_t count = ops->list_long(fs, "/", entries, 16);
  CHECK(count == 4);
  count = ops->list_long(fs, "/dir", entries, 16);
  CHECK(count == 1 && strcmp(entries[0].name, "sub") == 0 && entries[0].attributes == FS_ATTRIBUTE_DIRECTORY);
  CHECK(ops->list_long(fs, "/a.txt", entries, 16) == FS_ERROR_NOT_DIR);
  struct fs_directory_entry shorts[2];
  CHECK(ops->list(fs, "/", shorts, 2) == 2);

  int32_t fd = ops->open(fs, "/new.txt");
  CHECK(ops->seek(fs, fd, 2, SEEK_SET) == 2);
  CHECK(ops->read(fs, fd, text, 10) == 2 && memcmp(text, "to", 2) == 0);
  CHECK(ops->seek(fs, fd, -1, SEEK_END) == 3);
  CHECK(ops->seek(fs, fd, -10, SEEK_CUR) == FS_ERROR_INVALID);
  CHECK(ops->unlink(fs, "/new.txt") == FS_ERROR_BUSY);
  CHECK(ops->write_file(fs, "/new.txt", "z", 1) == FS_ERROR_BUSY);
  ops->close(fs, fd);
  CHECK(ops->unlink(fs, "/new.txt") == 0);
  CHECK(ops->open(fs, "/new.txt") == FS_ERROR_NOT_FOUND);

  CHECK(ops->rename(fs, "/a.txt", "b.txt") == 0);
  CHECK(ops->open(fs, "/a.txt") == FS_ERROR_NOT_FOUND);
  CHECK(read_all(fs, "/b.txt", text, sizeof(text)) == 2);
  CHECK(ops->rename(fs, "/b.txt", "appended.txt") == FS_ERROR_EXISTS);
  CHECK(ops->rename(fs, "/b.txt", "bad/name") == FS_ERROR_INVALID);
  CHECK(ops->rename(fs, "/", "x") == FS_ERROR_BUSY);

  CHECK(ops->move(fs, "/b.txt", "/dir") == 0);
  CHECK(read_all(fs, "/dir/b.txt", text, sizeof(text)) == 2);
  CHECK(ops->move(fs, "/dir", "/dir/sub") == FS_ERROR_INVALID);
  CHECK(ops->move(fs, "/dir/sub", "/") == 0);
  CHECK(read_all(fs, "/sub/f", text, sizeof(text)) == 4 && strcmp(text, "deep") == 0);
  CHECK(ops->move(fs, "/dir/b.txt", "/appended.txt") == FS_ERROR_NOT_DIR);

  CHECK(ops->unlink(fs, "/sub") == FS_ERROR_NOT_BLANK);
  CHECK(ops->unlink(fs, "/sub/f") == 0);
  CHECK(ops->unlink(fs, "/sub") == 0);
  CHECK(ops->unlink(fs, "/") == FS_ERROR_BUSY);

  char *big = malloc(9 * 1024 * 1024);
  memset(big, 'q', 9 * 1024 * 1024);
  CHECK(ops->write_file(fs, "/big", big, 9 * 1024 * 1024) == FS_ERROR_NO_SPACE);
  CHECK(ops->write_file(fs, "/big", big, 4 * 1024 * 1024) == 4 * 1024 * 1024);
  CHECK(ops->append_file(fs, "/big", big, 4 * 1024 * 1024) == 4 * 1024 * 1024);
  CHECK(ops->append_file(fs, "/big", big, 1) == FS_ERROR_NO_SPACE);
  CHECK(ops->write_file(fs, "/big", "s", 1) == 1);
  free(big);

  char name[16];
  int created = 0;
  for (int i = 0; i < 300; i++) {
    snprintf(name, sizeof(name), "/n%d", i);
    created += ops->create_file(fs, name) == 0;
  }
  CHECK(created > 200 && created < 256);
  CHECK(ops->create_file(fs, "/overflow") == FS_ERROR_NO_SPACE);

  int32_t held[40];
  int opened = 0;
  for (int i = 0; i < 40; i++) {
    held[i] = ops->open(fs, "/n0");
    opened += held[i] >= 0;
  }
  CHECK(opened == 32 && held[32] == FS_ERROR_NO_SPACE);
  for (int i = 0; i < 32; i++)
    ops->close(fs, held[i]);

  {
    struct file_stat_ex ex;
    void *fs = vfs_tmpfs_create();
    CHECK(fs != 0);
    CHECK(ops->create_file(fs, "/clock.txt") == 0);
    CHECK(ops->stat_ex(fs, "/clock.txt", &ex) == 0);
    CHECK(ex.mtime == 1700000000u && ex.ctime == 1700000000u && !ex.is_directory);
    fake_clock = 1700000100u;
    CHECK(ops->write_file(fs, "/clock.txt", "abc", 3) == 3);
    CHECK(ops->stat_ex(fs, "/clock.txt", &ex) == 0);
    CHECK(ex.size == 3 && ex.mtime == 1700000100u && ex.atime == 1700000000u);
    fake_clock = 1700000200u;
    CHECK(ops->append_file(fs, "/clock.txt", "de", 2) == 2);
    CHECK(ops->stat_ex(fs, "/clock.txt", &ex) == 0);
    CHECK(ex.size == 5 && ex.mtime == 1700000200u);
    CHECK(ops->stat_ex(fs, "/absent", &ex) == FS_ERROR_NOT_FOUND);
  }

  if (failures) {
    printf("tmpfs_test: %d failure(s)\n", failures);
    return 1;
  }
  printf("tmpfs_test: ok\n");
  return 0;
}
