#include "vfs_backends.h"
#include "vfs.h"
#include "../arch/x86_64/idt/include/idt.h"
#include "../drivers/interrupts/timer.h"
#include "../drivers/storage/block_device.h"
#include "../drivers/storage/storage_types.h"
#include "../kernel/process/process.h"
#include "../kernel/smp/cpu.h"
#include "../kernel/smp/cpufreq.h"
#include "../kernel/syscall/syscall.h"
#include "../lib/format.h"
#include "../lib/string.h"
#include "../mm/pmm.h"

#define PROCFS_MAX_OPEN 16
#define PROCFS_BUFFER 4096
#define PROCFS_MAX_PROCESSES 128

struct pbuf {
  char *data;
  uint32_t capacity;
  uint32_t length;
};

typedef void (*generator_fn)(struct pbuf *out, uint32_t pid);

struct proc_entry {
  const char *name;
  generator_fn generate;
};

struct procfs_file {
  bool used;
  uint32_t length;
  uint32_t position;
  char data[PROCFS_BUFFER];
};

static struct procfs_file files[PROCFS_MAX_OPEN];
static struct process_monitor_info process_list[PROCFS_MAX_PROCESSES];

static void emit(struct pbuf *out, const char *format, ...) {
  if (out->length >= out->capacity - 1)
    return;
  va_list args;
  va_start(args, format);
  int32_t written = kvformat(out->data + out->length,
                             out->capacity - out->length, format, args);
  va_end(args);
  if (written < 0)
    return;
  uint32_t room = out->capacity - 1 - out->length;
  out->length += (uint32_t)written < room ? (uint32_t)written : room;
}

static void generate_version(struct pbuf *out, uint32_t pid) {
  (void)pid;
  emit(out, "PureC OS kernel 1.1.0 (x86_64)\n");
}

static void generate_uptime(struct pbuf *out, uint32_t pid) {
  (void)pid;
  uint64_t milliseconds = timer_ticks();
  emit(out, "%llu.%02llu\n", milliseconds / 1000, (milliseconds % 1000) / 10);
}

static void generate_meminfo(struct pbuf *out, uint32_t pid) {
  (void)pid;
  uint64_t total = pmm_total_bytes() / 1024;
  uint64_t free = pmm_free_bytes() / 1024;
  emit(out, "MemTotal:    %8llu kB\n", total);
  emit(out, "MemFree:     %8llu kB\n", free);
  emit(out, "MemUsed:     %8llu kB\n", total - free);
}

static void brand_string(char brand[49]) {
  uint32_t maximum, ebx, ecx, edx;
  __asm__ volatile("cpuid"
                   : "=a"(maximum), "=b"(ebx), "=c"(ecx), "=d"(edx)
                   : "a"(0x80000000u), "c"(0));
  brand[0] = '\0';
  if (maximum < 0x80000004u)
    return;
  uint32_t words[12];
  for (uint32_t leaf = 0; leaf < 3; leaf++)
    __asm__ volatile("cpuid"
                     : "=a"(words[leaf * 4]), "=b"(words[leaf * 4 + 1]),
                       "=c"(words[leaf * 4 + 2]), "=d"(words[leaf * 4 + 3])
                     : "a"(0x80000002u + leaf), "c"(0));
  memcpy(brand, words, 48);
  brand[48] = '\0';
  char *start = brand;
  while (*start == ' ')
    start++;
  if (start != brand)
    memmove(brand, start, strlen(start) + 1);
}

static void generate_cpuinfo(struct pbuf *out, uint32_t pid) {
  (void)pid;
  char brand[49];
  brand_string(brand);
  uint32_t megahertz = cpufreq_measure_mhz();
  uint32_t count = cpu_registered_count();
  for (uint32_t id = 0; id < count; id++) {
    const struct cpu_info *info = cpu_get_info(id);
    emit(out, "processor\t: %u\n", id);
    emit(out, "model name\t: %s\n", brand[0] ? brand : "unknown");
    if (megahertz && id == 0)
      emit(out, "cpu MHz\t\t: %u\n", megahertz);
    emit(out, "apicid\t\t: %u\n", info ? info->lapic_id : 0);
    emit(out, "online\t\t: %s\n\n", cpu_is_online(id) ? "yes" : "no");
  }
}

static void generate_mounts(struct pbuf *out, uint32_t pid) {
  (void)pid;
  struct vfs_mount_info info;
  for (uint32_t index = 0; vfs_mounts_info(index, &info) == 0; index++)
    emit(out, "%s %s %s %s 0 0\n", info.source, info.path, info.type,
         (info.flags & 1U) ? "ro" : "rw");
}

static void generate_interrupts(struct pbuf *out, uint32_t pid) {
  (void)pid;
  for (uint32_t vector = 0; vector < 256; vector++) {
    uint64_t count = idt_vector_count((uint8_t)vector);
    if (count)
      emit(out, "%3u: %llu\n", vector, count);
  }
}

static void generate_filesystems(struct pbuf *out, uint32_t pid) {
  (void)pid;
  emit(out, "ext2\ninitramfs\nnodev\tdevfs\nnodev\tkernelfs\nnodev\tprocfs\nnodev\tsysfs\n");
}

static void generate_partitions(struct pbuf *out, uint32_t pid) {
  (void)pid;
  struct storage_device_info devices[16];
  int32_t count = block_device_list(devices, 16);
  emit(out, "name      sectors  sector-size  model\n");
  for (int32_t i = 0; i < count; i++)
    emit(out, "%-8s %8llu %12u  %s\n", devices[i].name, devices[i].sector_count,
         devices[i].sector_size, devices[i].model);
}

static const struct process_monitor_info *find_process(uint32_t pid) {
  int32_t count = process_monitor_list(process_list, PROCFS_MAX_PROCESSES);
  for (int32_t i = 0; i < count; i++)
    if (process_list[i].pid == pid)
      return &process_list[i];
  return 0;
}

static const char *state_name(uint32_t state) {
  if (state == PROCESS_MONITOR_STATE_READY)
    return "ready";
  if (state == PROCESS_MONITOR_STATE_RUNNING)
    return "running";
  if (state == PROCESS_MONITOR_STATE_EXITED)
    return "exited";
  return "unknown";
}

static void generate_pid_status(struct pbuf *out, uint32_t pid) {
  const struct process_monitor_info *process = find_process(pid);
  if (!process)
    return;
  emit(out, "Name:\t%s\n", process->name);
  emit(out, "State:\t%s\n", state_name(process->state));
  emit(out, "Pid:\t%u\n", process->pid);
  emit(out, "PPid:\t%u\n", process->parent_pid);
  emit(out, "VmRSS:\t%llu kB\n", process->resident_bytes / 1024);
  emit(out, "CpuTime:\t%llu ms\n", process->runtime_ms);
  emit(out, "Cpu:\t%u%%\n", process->cpu_percent);
}

static void generate_pid_name(struct pbuf *out, uint32_t pid) {
  const struct process_monitor_info *process = find_process(pid);
  if (process)
    emit(out, "%s\n", process->name);
}

static const struct proc_entry root_entries[] = {
    {"version", generate_version},       {"uptime", generate_uptime},
    {"meminfo", generate_meminfo},       {"cpuinfo", generate_cpuinfo},
    {"mounts", generate_mounts},         {"interrupts", generate_interrupts},
    {"filesystems", generate_filesystems},
    {"partitions", generate_partitions}};

static const struct proc_entry pid_entries[] = {
    {"status", generate_pid_status}, {"name", generate_pid_name}};

#define ROOT_COUNT (sizeof(root_entries) / sizeof(root_entries[0]))
#define PID_COUNT (sizeof(pid_entries) / sizeof(pid_entries[0]))

enum node_kind { NODE_NONE, NODE_ROOT, NODE_FILE, NODE_PID_DIR };

struct node {
  enum node_kind kind;
  generator_fn generate;
  uint32_t pid;
};

static bool parse_pid(const char *text, uint32_t *pid, uint32_t *length) {
  uint32_t value = 0, digits = 0;
  while (text[digits] >= '0' && text[digits] <= '9') {
    value = value * 10 + (uint32_t)(text[digits] - '0');
    if (++digits > 9)
      return false;
  }
  if (!digits)
    return false;
  *pid = value;
  *length = digits;
  return true;
}

static struct node lookup(const char *path) {
  struct node node = {NODE_NONE, 0, 0};
  if (!path || path[0] != '/')
    return node;
  if (!path[1]) {
    node.kind = NODE_ROOT;
    return node;
  }
  const char *name = path + 1;
  uint32_t pid, digits;
  if (parse_pid(name, &pid, &digits)) {
    if (!find_process(pid))
      return node;
    if (!name[digits]) {
      node.kind = NODE_PID_DIR;
      node.pid = pid;
      return node;
    }
    if (name[digits] != '/')
      return node;
    for (uint32_t i = 0; i < PID_COUNT; i++)
      if (strcmp(name + digits + 1, pid_entries[i].name) == 0) {
        node.kind = NODE_FILE;
        node.generate = pid_entries[i].generate;
        node.pid = pid;
        return node;
      }
    return node;
  }
  for (uint32_t i = 0; i < ROOT_COUNT; i++)
    if (strcmp(name, root_entries[i].name) == 0) {
      node.kind = NODE_FILE;
      node.generate = root_entries[i].generate;
      return node;
    }
  return node;
}

static int32_t procfs_open(void *context, const char *path) {
  (void)context;
  struct node node = lookup(path);
  if (node.kind == NODE_NONE)
    return FS_ERROR_NOT_FOUND;
  if (node.kind != NODE_FILE)
    return FS_ERROR_NOT_FILE;
  for (int32_t i = 0; i < PROCFS_MAX_OPEN; i++) {
    if (files[i].used)
      continue;
    struct pbuf out = {files[i].data, PROCFS_BUFFER, 0};
    files[i].data[0] = '\0';
    node.generate(&out, node.pid);
    files[i].used = true;
    files[i].length = out.length;
    files[i].position = 0;
    return i;
  }
  return FS_ERROR_NO_SPACE;
}

static struct procfs_file *get_file(int32_t descriptor) {
  if (descriptor < 0 || descriptor >= PROCFS_MAX_OPEN || !files[descriptor].used)
    return 0;
  return &files[descriptor];
}

static int32_t procfs_read(void *context, int32_t descriptor, void *buffer,
                           uint32_t count) {
  (void)context;
  struct procfs_file *file = get_file(descriptor);
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

static int64_t procfs_seek(void *context, int32_t descriptor, int64_t offset,
                           uint32_t whence) {
  (void)context;
  struct procfs_file *file = get_file(descriptor);
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

static int32_t procfs_close(void *context, int32_t descriptor) {
  (void)context;
  struct procfs_file *file = get_file(descriptor);
  if (!file)
    return FS_ERROR_INVALID;
  file->used = false;
  return 0;
}

static int32_t procfs_stat(void *context, const char *path,
                           struct file_stat_info *out) {
  (void)context;
  struct node node = lookup(path);
  if (node.kind == NODE_NONE)
    return FS_ERROR_NOT_FOUND;
  out->size = 0;
  out->is_directory = node.kind != NODE_FILE;
  out->reserved = FS_NODE_FILE;
  return 0;
}

static void fill_short(struct fs_directory_entry *entry, const char *name,
                       uint8_t attributes) {
  strncpy(entry->name, name, FS_DIRECTORY_NAME_CAPACITY - 1);
  entry->name[FS_DIRECTORY_NAME_CAPACITY - 1] = '\0';
  entry->size = 0;
  entry->attributes = attributes;
}

static void fill_long(struct fs_directory_entry_long *entry, const char *name,
                      uint8_t attributes) {
  strncpy(entry->name, name, FS_LONG_NAME_CAPACITY - 1);
  entry->name[FS_LONG_NAME_CAPACITY - 1] = '\0';
  entry->size = 0;
  entry->attributes = attributes;
}

static int32_t list_node(const char *path, void *entries, uint32_t capacity,
                         bool long_names) {
  struct node node = lookup(path);
  if (node.kind == NODE_NONE)
    return FS_ERROR_NOT_FOUND;
  if (node.kind == NODE_FILE)
    return FS_ERROR_NOT_DIR;
  uint32_t count = 0;
  char name[16];
#define ADD(text, attributes)                                                  \
  do {                                                                         \
    if (count < capacity) {                                                    \
      if (long_names)                                                          \
        fill_long(&((struct fs_directory_entry_long *)entries)[count], text,   \
                  attributes);                                                 \
      else                                                                     \
        fill_short(&((struct fs_directory_entry *)entries)[count], text,       \
                   attributes);                                                \
      count++;                                                                 \
    }                                                                          \
  } while (0)
  if (node.kind == NODE_ROOT) {
    for (uint32_t i = 0; i < ROOT_COUNT; i++)
      ADD(root_entries[i].name, 0);
    int32_t processes = process_monitor_list(process_list, PROCFS_MAX_PROCESSES);
    for (int32_t i = 0; i < processes; i++) {
      kformat(name, sizeof(name), "%u", process_list[i].pid);
      ADD(name, FS_ATTRIBUTE_DIRECTORY);
    }
  } else {
    for (uint32_t i = 0; i < PID_COUNT; i++)
      ADD(pid_entries[i].name, 0);
  }
#undef ADD
  return (int32_t)count;
}

static int32_t procfs_list(void *context, const char *path,
                           struct fs_directory_entry *entries,
                           uint32_t capacity) {
  (void)context;
  return list_node(path, entries, capacity, false);
}

static int32_t procfs_list_long(void *context, const char *path,
                                struct fs_directory_entry_long *entries,
                                uint32_t capacity) {
  (void)context;
  return list_node(path, entries, capacity, true);
}

const struct vfs_fs_ops vfs_procfs_ops = {
    .name = "procfs",
    .open = procfs_open,
    .read = procfs_read,
    .seek = procfs_seek,
    .close = procfs_close,
    .stat = procfs_stat,
    .list = procfs_list,
    .list_long = procfs_list_long,
};
