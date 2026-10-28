#include "../src/drivers/storage/storage_types.h"
#include "../src/fs/vfs.h"
#include "../src/fs/vfs_backends.h"
#include "../src/kernel/process/process.h"
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

uint64_t timer_ticks(void) { return 123456; }
uint64_t pmm_total_bytes(void) { return 512ULL * 1024 * 1024; }
uint64_t pmm_free_bytes(void) { return 300ULL * 1024 * 1024; }
uint64_t idt_vector_count(uint8_t vector) {
  return vector == 32 ? 99 : vector == 0xF1 ? 5 : 0;
}
uint32_t cpufreq_measure_mhz(void) { return 2400; }
uint32_t cpu_registered_count(void) { return 2; }
static struct cpu_info cpus[2] = {{0, 0, 0, true, true}, {1, 1, 2, true, false}};
const struct cpu_info *cpu_get_info(uint32_t id) { return id < 2 ? &cpus[id] : 0; }
bool cpu_is_online(uint32_t id) { return id == 0; }
int32_t block_device_list(struct storage_device_info *devices, uint32_t capacity) {
  (void)capacity;
  memset(&devices[0], 0, sizeof(devices[0]));
  strcpy(devices[0].name, "sda");
  strcpy(devices[0].model, "TestDisk");
  devices[0].sector_count = 2048;
  devices[0].sector_size = 512;
  return 1;
}
int32_t process_monitor_list(struct process_monitor_info *entries, uint32_t capacity) {
  (void)capacity;
  memset(entries, 0, 2 * sizeof(*entries));
  entries[0].pid = 1;
  entries[0].state = PROCESS_MONITOR_STATE_RUNNING;
  strcpy(entries[0].name, "init");
  entries[0].resident_bytes = 8192;
  entries[1].pid = 12;
  entries[1].parent_pid = 1;
  entries[1].state = PROCESS_MONITOR_STATE_READY;
  entries[1].runtime_ms = 77;
  entries[1].cpu_percent = 4;
  strcpy(entries[1].name, "shell");
  return 2;
}
int32_t vfs_mounts_info(uint32_t index, struct vfs_mount_info *out) {
  if (index > 1)
    return FS_ERROR_NOT_FOUND;
  strcpy(out->path, index ? "/proc" : "/");
  strcpy(out->source, index ? "proc" : "sda1");
  strcpy(out->type, index ? "procfs" : "ext2");
  out->flags = index ? 1 : 0;
  return 0;
}

static const struct vfs_fs_ops *ops = &vfs_procfs_ops;

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
  char text[4096];
  CHECK(read_file("/uptime", text, sizeof(text)) > 0);
  CHECK(strcmp(text, "123.45\n") == 0);
  CHECK(read_file("/meminfo", text, sizeof(text)) > 0);
  CHECK(strstr(text, "MemTotal:") && strstr(text, "524288 kB"));
  CHECK(strstr(text, "MemFree:") && strstr(text, "307200 kB"));
  CHECK(read_file("/version", text, sizeof(text)) > 0 && strstr(text, "PureC OS"));
  CHECK(read_file("/mounts", text, sizeof(text)) > 0);
  CHECK(strstr(text, "sda1 / ext2 rw 0 0\n"));
  CHECK(strstr(text, "proc /proc procfs ro 0 0\n"));
  CHECK(read_file("/interrupts", text, sizeof(text)) > 0);
  CHECK(strstr(text, " 32: 99\n") && strstr(text, "241: 5\n"));
  CHECK(read_file("/cpuinfo", text, sizeof(text)) > 0);
  CHECK(strstr(text, "processor\t: 1\n") && strstr(text, "cpu MHz\t\t: 2400"));
  CHECK(strstr(text, "online\t\t: no"));
  CHECK(read_file("/partitions", text, sizeof(text)) > 0);
  CHECK(strstr(text, "sda") && strstr(text, "TestDisk"));
  CHECK(read_file("/12/status", text, sizeof(text)) > 0);
  CHECK(strstr(text, "Name:\tshell\n") && strstr(text, "State:\tready\n"));
  CHECK(strstr(text, "PPid:\t1\n") && strstr(text, "CpuTime:\t77 ms\n"));
  CHECK(strstr(text, "Cpu:\t4%\n"));
  CHECK(read_file("/1/name", text, sizeof(text)) > 0 && strcmp(text, "init\n") == 0);
  CHECK(read_file("/99/status", text, sizeof(text)) == FS_ERROR_NOT_FOUND);
  CHECK(read_file("/nosuch", text, sizeof(text)) == FS_ERROR_NOT_FOUND);
  CHECK(read_file("/12/nosuch", text, sizeof(text)) == FS_ERROR_NOT_FOUND);
  CHECK(ops->open(0, "/") == FS_ERROR_NOT_FILE);
  CHECK(ops->open(0, "/12") == FS_ERROR_NOT_FILE);

  int32_t fd = ops->open(0, "/uptime");
  char two[3];
  CHECK(ops->read(0, fd, two, 3) == 3);
  CHECK(ops->seek(0, fd, 0, SEEK_SET) == 0);
  CHECK(ops->read(0, fd, two, 1) == 1 && two[0] == '1');
  CHECK(ops->seek(0, fd, 0, SEEK_END) == 7);
  CHECK(ops->read(0, fd, two, 3) == 0);
  CHECK(ops->seek(0, fd, -100, SEEK_CUR) == FS_ERROR_INVALID);
  ops->close(0, fd);
  CHECK(ops->read(0, fd, two, 1) == FS_ERROR_INVALID);

  struct file_stat_info info;
  CHECK(ops->stat(0, "/", &info) == 0 && info.is_directory);
  CHECK(ops->stat(0, "/12", &info) == 0 && info.is_directory);
  CHECK(ops->stat(0, "/uptime", &info) == 0 && !info.is_directory);
  CHECK(ops->stat(0, "/77", &info) == FS_ERROR_NOT_FOUND);

  struct fs_directory_entry_long entries[32];
  int32_t count = ops->list_long(0, "/", entries, 32);
  CHECK(count == 10);
  CHECK(strcmp(entries[0].name, "version") == 0);
  CHECK(strcmp(entries[8].name, "1") == 0 && entries[8].attributes == FS_ATTRIBUTE_DIRECTORY);
  CHECK(strcmp(entries[9].name, "12") == 0);
  count = ops->list_long(0, "/12", entries, 32);
  CHECK(count == 2 && strcmp(entries[0].name, "status") == 0);
  CHECK(ops->list_long(0, "/uptime", entries, 32) == FS_ERROR_NOT_DIR);
  struct fs_directory_entry shorts[4];
  CHECK(ops->list(0, "/", shorts, 4) == 4);

  int32_t held[20];
  int opened = 0;
  for (int i = 0; i < 20; i++) {
    held[i] = ops->open(0, "/version");
    opened += held[i] >= 0;
  }
  CHECK(opened == 16 && held[16] == FS_ERROR_NO_SPACE);
  for (int i = 0; i < 16; i++)
    ops->close(0, held[i]);

  if (failures) {
    printf("procfs_test: %d failure(s)\n", failures);
    return 1;
  }
  printf("procfs_test: ok\n");
  return 0;
}
