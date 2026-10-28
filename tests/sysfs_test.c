#include "../src/drivers/pci/pci.h"
#include "../src/drivers/storage/storage_types.h"
#include "../src/fs/vfs.h"
#include "../src/fs/vfs_backends.h"
#include "../src/kernel/smp/cpu.h"
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

uint32_t cpu_registered_count(void) { return 2; }
static struct cpu_info cpus[2] = {{0, 0, 0, true, true}, {1, 1, 2, true, false}};
const struct cpu_info *cpu_get_info(uint32_t id) { return id < 2 ? &cpus[id] : 0; }
bool cpu_is_online(uint32_t id) { return id == 0; }
void pci_enumerate(pci_device_visitor visitor, void *context) {
  struct pci_device_info device = {0x8086, 0x100e, 0, 3, 0, 2, 0, 0, 3};
  visitor(&device, context);
  struct pci_device_info second = {0x1234, 0x1111, 0, 0x1f, 2, 3, 0, 0, 0};
  visitor(&second, context);
}
int32_t block_device_list(struct storage_device_info *devices, uint32_t capacity) {
  (void)capacity;
  memset(&devices[0], 0, sizeof(devices[0]));
  strcpy(devices[0].name, "sda");
  strcpy(devices[0].model, "TestDisk");
  devices[0].sector_count = 2048;
  devices[0].sector_size = 512;
  devices[0].writable = 1;
  return 1;
}

static const struct vfs_fs_ops *ops = &vfs_sysfs_ops;

static int read_file(const char *path, char *buffer, uint32_t capacity) {
  int32_t fd = ops->open(0, path);
  if (fd < 0)
    return fd;
  int32_t n = ops->read(0, fd, buffer, capacity - 1);
  ops->close(0, fd);
  if (n >= 0)
    buffer[n] = '\0';
  return n;
}

int main(void) {
  char text[256];
  CHECK(read_file("/kernel/version", text, sizeof(text)) > 0 &&
        strstr(text, "PureC OS"));
  CHECK(read_file("/devices/system/cpu/cpu0/online", text, sizeof(text)) > 0 &&
        strcmp(text, "1\n") == 0);
  CHECK(read_file("/devices/system/cpu/cpu1/online", text, sizeof(text)) > 0 &&
        strcmp(text, "0\n") == 0);
  CHECK(read_file("/devices/system/cpu/cpu1/apicid", text, sizeof(text)) > 0 &&
        strcmp(text, "2\n") == 0);
  CHECK(read_file("/bus/pci/devices/00:03.0/vendor", text, sizeof(text)) > 0 &&
        strcmp(text, "0x8086\n") == 0);
  CHECK(read_file("/bus/pci/devices/00:03.0/device", text, sizeof(text)) > 0 &&
        strcmp(text, "0x100e\n") == 0);
  CHECK(read_file("/bus/pci/devices/00:03.0/class", text, sizeof(text)) > 0 &&
        strcmp(text, "0x020000\n") == 0);
  CHECK(read_file("/bus/pci/devices/00:1f.2/revision", text, sizeof(text)) > 0 &&
        strcmp(text, "0x00\n") == 0);
  CHECK(read_file("/block/sda/size", text, sizeof(text)) > 0 &&
        strcmp(text, "2048\n") == 0);
  CHECK(read_file("/block/sda/model", text, sizeof(text)) > 0 &&
        strcmp(text, "TestDisk\n") == 0);
  CHECK(read_file("/block/sda/ro", text, sizeof(text)) > 0 &&
        strcmp(text, "0\n") == 0);
  CHECK(read_file("/nosuch", text, sizeof(text)) == FS_ERROR_NOT_FOUND);
  CHECK(read_file("/block/sdb/size", text, sizeof(text)) == FS_ERROR_NOT_FOUND);
  CHECK(ops->open(0, "/") == FS_ERROR_NOT_FILE);
  CHECK(ops->open(0, "/bus/pci/devices") == FS_ERROR_NOT_FILE);

  struct file_stat_info info;
  CHECK(ops->stat(0, "/", &info) == 0 && info.is_directory);
  CHECK(ops->stat(0, "/bus/pci/devices/", &info) == 0 && info.is_directory);
  CHECK(ops->stat(0, "/kernel/version", &info) == 0 && !info.is_directory);
  CHECK(ops->stat(0, "/kernel/nosuch", &info) == FS_ERROR_NOT_FOUND);

  struct fs_directory_entry_long entries[32];
  int32_t count = ops->list_long(0, "/", entries, 32);
  CHECK(count == 4);
  CHECK(strcmp(entries[0].name, "kernel") == 0 &&
        entries[0].attributes == FS_ATTRIBUTE_DIRECTORY);
  count = ops->list_long(0, "/bus/pci/devices", entries, 32);
  CHECK(count == 2 && strcmp(entries[0].name, "00:03.0") == 0 &&
        strcmp(entries[1].name, "00:1f.2") == 0);
  count = ops->list_long(0, "/bus/pci/devices/00:03.0", entries, 32);
  CHECK(count == 4 && strcmp(entries[0].name, "vendor") == 0);
  count = ops->list_long(0, "/devices/system/cpu", entries, 32);
  CHECK(count == 2 && strcmp(entries[1].name, "cpu1") == 0);
  count = ops->list_long(0, "/block", entries, 32);
  CHECK(count == 1 && strcmp(entries[0].name, "sda") == 0);
  CHECK(ops->list_long(0, "/kernel/version", entries, 32) == FS_ERROR_NOT_DIR);
  struct fs_directory_entry shorts[2];
  CHECK(ops->list(0, "/bus/pci/devices", shorts, 1) == 1);

  int32_t held[20];
  int opened = 0;
  for (int i = 0; i < 20; i++) {
    held[i] = ops->open(0, "/kernel/version");
    opened += held[i] >= 0;
  }
  CHECK(opened == 16 && held[16] == FS_ERROR_NO_SPACE);
  for (int i = 0; i < 16; i++)
    ops->close(0, held[i]);

  if (failures) {
    printf("sysfs_test: %d failure(s)\n", failures);
    return 1;
  }
  printf("sysfs_test: ok\n");
  return 0;
}
