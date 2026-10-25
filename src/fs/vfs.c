#include "../kernel/sync/mutex.h"
static mutex_t vfs_mutex;
#include "../drivers/storage/block_device.h"
#include "../kernel/diagnostics/klog.h"
#include "../kernel/syscall/syscall.h"
#include "../lib/string.h"
#include "../mm/pmm.h"
#include "./ext2/include/ext2.h"
#include "./ext2/include/ext2_block.h"
#include "./ext2/include/ext2_debug.h"
#include "./ext2/include/ext2_dir.h"
#include "./ext2/include/ext2_inode.h"
#include "./ext2/include/ext2_types.h"
#include "./fat/include/fat32.h"
#include "initramfs.h"
#include "vfs.h"
#include "../kernel/core/system_info.h"

#define VFS_HANDLE_INITIAL 32
#define VIRT_BUF_SIZE 512

enum vfs_handle_type {
  VFS_HANDLE_NONE,
  VFS_HANDLE_FAT32,
  VFS_HANDLE_EXT2,
  VFS_HANDLE_INITRAMFS,
  VFS_HANDLE_KERNEL_FILE,
  VFS_HANDLE_KLOG,
  VFS_HANDLE_VIRTUAL
};

enum vfs_virtual_kind {
  VIRT_NONE = 0,
  // /dev
  VIRT_DEV_NULL,
  VIRT_DEV_ZERO,
  VIRT_DEV_FULL,
  VIRT_DEV_RANDOM,
  VIRT_DEV_URANDOM,
  VIRT_DEV_CONSOLE,
  VIRT_DEV_TTY,
  // /proc
  VIRT_PROC_VERSION,
  VIRT_PROC_UPTIME,
  VIRT_PROC_MEMINFO,
  VIRT_PROC_CPUINFO,
  VIRT_PROC_FILESYSTEMS,
  VIRT_PROC_MOUNTS,
  VIRT_PROC_CMDLINE,
  VIRT_PROC_KERNEL_VERSION,
  VIRT_PROC_KERNEL_PANIC,
  VIRT_PROC_KERNEL_INIT,
  VIRT_PROC_KERNEL_ABI,
  // /sys
  VIRT_SYS_CLASS_BLOCK,
  VIRT_SYS_DEVICES,
  VIRT_SYS_KERNEL_VERSION
};

static uint8_t vfs_active_fs = VFS_FS_EXT2;

struct kernel_file {
  const char *path;
  const char *name;
  const char *content;
};

struct vfs_handle {
  enum vfs_handle_type type;
  int32_t backend_descriptor;
  const char *data;
  uint32_t size;
  uint32_t position;
  uint64_t klog_cursor;
  bool klog_data_lost;
  uint8_t virt_kind;
  char virt_buf[VIRT_BUF_SIZE];
  uint32_t virt_size;
};

static const struct kernel_file kernel_files[] = {
    {"/proc/version", "version", "PureC OS kernel 1.1.0\n"},
    {"/proc/panic", "panic.cfg", "panic-screan=classic\n #panic-screan=qr\n"},
    {"/proc/init", "init", "pid1=/bin/init shell=/usr/bin/terminal editor=/usr/bin/nano\n"},
    {"/proc/abi", "abi",
     "syscall=int80 process=exec,args,env,wait,exit fd=per-process "
     "vfs=ext2\n"}};

static struct vfs_handle *handles;

static bool path_equals(const char *left, const char *right) {
  return left && right && strcmp(left, right) == 0;
}

static void copy_name(char destination[FS_DIRECTORY_NAME_CAPACITY],
                      const char *source) {
  uint32_t i = 0;
  while (source[i] && i + 1 < FS_DIRECTORY_NAME_CAPACITY) {
    destination[i] = source[i];
    i++;
  }
  destination[i] = '\0';
}

static const struct kernel_file *find_kernel_file(const char *path) {
  for (uint32_t i = 0; i < sizeof(kernel_files) / sizeof(kernel_files[0]);
       i++) {
    if (path_equals(path, kernel_files[i].path))
      return &kernel_files[i];
  }
  return 0;
}

static uint8_t virtual_kind_from_path(const char *path) {
  if (!path) return VIRT_NONE;
  if (strcmp(path, "/dev/null") == 0) return VIRT_DEV_NULL;
  if (strcmp(path, "/dev/zero") == 0) return VIRT_DEV_ZERO;
  if (strcmp(path, "/dev/full") == 0) return VIRT_DEV_FULL;
  if (strcmp(path, "/dev/random") == 0) return VIRT_DEV_RANDOM;
  if (strcmp(path, "/dev/urandom") == 0) return VIRT_DEV_URANDOM;
  if (strcmp(path, "/dev/console") == 0) return VIRT_DEV_CONSOLE;
  if (strcmp(path, "/dev/tty") == 0) return VIRT_DEV_TTY;
  if (strcmp(path, "/proc/version") == 0) return VIRT_PROC_VERSION;
  if (strcmp(path, "/proc/uptime") == 0) return VIRT_PROC_UPTIME;
  if (strcmp(path, "/proc/meminfo") == 0) return VIRT_PROC_MEMINFO;
  if (strcmp(path, "/proc/cpuinfo") == 0) return VIRT_PROC_CPUINFO;
  if (strcmp(path, "/proc/filesystems") == 0) return VIRT_PROC_FILESYSTEMS;
  if (strcmp(path, "/proc/mounts") == 0) return VIRT_PROC_MOUNTS;
  if (strcmp(path, "/proc/cmdline") == 0) return VIRT_PROC_CMDLINE;
  if (strcmp(path, "/proc/kernel/version") == 0) return VIRT_PROC_KERNEL_VERSION;
  if (strcmp(path, "/proc/kernel/panic") == 0) return VIRT_PROC_KERNEL_PANIC;
  if (strcmp(path, "/proc/kernel/init") == 0) return VIRT_PROC_KERNEL_INIT;
  if (strcmp(path, "/proc/kernel/abi") == 0) return VIRT_PROC_KERNEL_ABI;
  if (strcmp(path, "/sys/class/block") == 0) return VIRT_SYS_CLASS_BLOCK;
  if (strcmp(path, "/sys/devices") == 0) return VIRT_SYS_DEVICES;
  if (strcmp(path, "/sys/kernel/version") == 0) return VIRT_SYS_KERNEL_VERSION;
  return VIRT_NONE;
}

static void copy_name_long(char destination[FS_LONG_NAME_CAPACITY],
                           const char *source);

static uint32_t virt_append(char *buf, uint32_t capacity, uint32_t pos,
                            const char *text) {
  while (text && *text && pos + 1 < capacity)
    buf[pos++] = *text++;
  buf[pos] = '\0';
  return pos;
}

static uint32_t virt_append_u64(char *buf, uint32_t capacity, uint32_t pos,
                                uint64_t value, uint32_t min_digits) {
  char digits[24];
  uint32_t n = 0;
  if (value == 0) {
    digits[n++] = '0';
  } else {
    while (value > 0 && n < sizeof(digits)) {
      digits[n++] = (char)('0' + (value % 10));
      value /= 10;
    }
  }
  while (n < min_digits && n < sizeof(digits))
    digits[n++] = '0';
  while (n > 0 && pos + 1 < capacity)
    buf[pos++] = digits[--n];
  buf[pos] = '\0';
  return pos;
}

static void virtual_file_generate(uint8_t kind, char *buf, uint32_t *out_size) {
  uint32_t p = 0;
  buf[0] = '\0';
  switch (kind) {
    case VIRT_PROC_VERSION:
      p = virt_append(buf, VIRT_BUF_SIZE, p, "PureC OS kernel 1.1.0\n");
      break;
    case VIRT_PROC_UPTIME: {
      uint64_t s = system_info_uptime_ms() / 1000;
      p = virt_append_u64(buf, VIRT_BUF_SIZE, p, s / 100, 0);
      p = virt_append(buf, VIRT_BUF_SIZE, p, ".");
      p = virt_append_u64(buf, VIRT_BUF_SIZE, p, s % 100, 2);
      p = virt_append(buf, VIRT_BUF_SIZE, p, " 0.00\n");
      break;
    }
    case VIRT_PROC_MEMINFO: {
      uint64_t total = system_info_total_ram_bytes() / 1024;
      uint64_t free = system_info_usable_ram_bytes() / 1024;
      p = virt_append(buf, VIRT_BUF_SIZE, p, "MemTotal: ");
      p = virt_append_u64(buf, VIRT_BUF_SIZE, p, total, 0);
      p = virt_append(buf, VIRT_BUF_SIZE, p, " kB\nMemFree: ");
      p = virt_append_u64(buf, VIRT_BUF_SIZE, p, free, 0);
      p = virt_append(buf, VIRT_BUF_SIZE, p, " kB\nMemAvailable: ");
      p = virt_append_u64(buf, VIRT_BUF_SIZE, p, free, 0);
      p = virt_append(buf, VIRT_BUF_SIZE, p, " kB\n");
      break;
    }
    case VIRT_PROC_CPUINFO: {
      const char *name = system_info_cpu_name();
      p = virt_append(buf, VIRT_BUF_SIZE, p, "processor\t: 0\nvendor_id\t: PureC\ncpu family\t: 6\nmodel\t\t: 0\nmodel name\t: ");
      p = virt_append(buf, VIRT_BUF_SIZE, p, name ? name : "unknown");
      p = virt_append(buf, VIRT_BUF_SIZE, p, "\ncpu MHz\t\t: ");
      p = virt_append_u64(buf, VIRT_BUF_SIZE, p,
                          system_info_tsc_frequency_hz() / 1000000, 0);
      p = virt_append(buf, VIRT_BUF_SIZE, p, "\n");
      break;
    }
    case VIRT_PROC_FILESYSTEMS:
      p = virt_append(buf, VIRT_BUF_SIZE, p, "nodev\tinitramfs\next2\n");
      break;
    case VIRT_PROC_MOUNTS:
      p = virt_append(buf, VIRT_BUF_SIZE, p, "none / initramfs ro 0 0\n");
      break;
    case VIRT_PROC_CMDLINE:
      p = virt_append(buf, VIRT_BUF_SIZE, p, "BOOT_IMAGE=/boot/kernel.elf\n");
      break;
    case VIRT_PROC_KERNEL_VERSION:
      p = virt_append(buf, VIRT_BUF_SIZE, p, "PureC OS kernel 1.1.0\n");
      break;
    case VIRT_PROC_KERNEL_PANIC:
      p = virt_append(buf, VIRT_BUF_SIZE, p,
                      "panic-screan=classic\n #panic-screan=qr\n");
      break;
    case VIRT_PROC_KERNEL_INIT:
      p = virt_append(buf, VIRT_BUF_SIZE, p,
                      "pid1=/bin/init shell=/usr/bin/terminal editor=/usr/bin/nano\n");
      break;
    case VIRT_PROC_KERNEL_ABI:
      p = virt_append(buf, VIRT_BUF_SIZE, p,
                      "syscall=int80 process=exec,args,env,wait,exit fd=per-process vfs=ext2\n");
      break;
    case VIRT_SYS_KERNEL_VERSION:
      p = virt_append(buf, VIRT_BUF_SIZE, p, "PureC OS kernel 1.1.0\n");
      break;
    default:
      break;
  }
  *out_size = p;
}

static bool is_virtual_dir(const char *path) {
  return path &&
    (strcmp(path, "/dev") == 0 || strcmp(path, "/proc") == 0 ||
     strcmp(path, "/sys") == 0 || strcmp(path, "/proc/kernel") == 0 ||
     strcmp(path, "/sys/class") == 0 || strcmp(path, "/sys/class/block") == 0 ||
     strcmp(path, "/sys/devices") == 0 || strcmp(path, "/sys/kernel") == 0);
}

static int32_t virtual_dir_list(const char *path, struct fs_directory_entry *entries,
                                uint32_t capacity, bool long_names) {
  (void)long_names;
  if (!entries || capacity == 0) return FS_ERROR_INVALID;
  uint32_t count = 0;
  if (strcmp(path, "/dev") == 0) {
    const char *devs[] = {"null", "zero", "full", "random", "urandom", "console", "tty"};
    for (uint32_t i = 0; i < sizeof(devs)/sizeof(devs[0]) && count < capacity; i++) {
      copy_name(entries[count].name, devs[i]);
      entries[count].size = 0;
      entries[count].attributes = 0;
      count++;
    }
  } else if (strcmp(path, "/proc") == 0) {
    const char *procs[] = {"version", "uptime", "meminfo", "cpuinfo", "filesystems",
                           "mounts", "cmdline", "kernel"};
    for (uint32_t i = 0; i < sizeof(procs)/sizeof(procs[0]) && count < capacity; i++) {
      copy_name(entries[count].name, procs[i]);
      entries[count].size = (strcmp(procs[i], "kernel") == 0) ? 0 : 1;
      entries[count].attributes = (strcmp(procs[i], "kernel") == 0) ? FS_ATTRIBUTE_DIRECTORY : 0;
      count++;
    }
  } else if (strcmp(path, "/sys") == 0) {
    const char *syss[] = {"class", "devices", "kernel"};
    for (uint32_t i = 0; i < sizeof(syss)/sizeof(syss[0]) && count < capacity; i++) {
      copy_name(entries[count].name, syss[i]);
      entries[count].size = 0;
      entries[count].attributes = FS_ATTRIBUTE_DIRECTORY;
      count++;
    }
  } else if (strcmp(path, "/proc/kernel") == 0) {
    const char *kerns[] = {"version", "panic", "init", "abi"};
    for (uint32_t i = 0; i < sizeof(kerns)/sizeof(kerns[0]) && count < capacity; i++) {
      copy_name(entries[count].name, kerns[i]);
      entries[count].size = 1;
      entries[count].attributes = 0;
      count++;
    }
  } else if (strcmp(path, "/sys/class") == 0) {
    if (count < capacity) {
      copy_name(entries[count].name, "block");
      entries[count].size = 0;
      entries[count].attributes = FS_ATTRIBUTE_DIRECTORY;
      count++;
    }
  } else if (strcmp(path, "/sys/class/block") == 0) {
    // list block devices dynamically? For now empty
  } else if (strcmp(path, "/sys/devices") == 0) {
    // empty for now
  } else if (strcmp(path, "/sys/kernel") == 0) {
    if (count < capacity) {
      copy_name(entries[count].name, "version");
      entries[count].size = 1;
      entries[count].attributes = 0;
      count++;
    }
  }
  return (int32_t)count;
}

static int32_t virtual_dir_list_long(const char *path,
                                     struct fs_directory_entry_long *entries,
                                     uint32_t capacity) {
  if (!entries || capacity == 0) return FS_ERROR_INVALID;
  uint32_t count = 0;
  if (strcmp(path, "/dev") == 0) {
    const char *devs[] = {"null", "zero", "full", "random", "urandom", "console", "tty"};
    for (uint32_t i = 0; i < sizeof(devs)/sizeof(devs[0]) && count < capacity; i++) {
      copy_name_long(entries[count].name, devs[i]);
      entries[count].size = 0;
      entries[count].attributes = 0;
      entries[count].reserved[0] = entries[count].reserved[1] = entries[count].reserved[2] = 0;
      count++;
    }
  } else if (strcmp(path, "/proc") == 0) {
    const char *procs[] = {"version", "uptime", "meminfo", "cpuinfo", "filesystems",
                           "mounts", "cmdline", "kernel"};
    for (uint32_t i = 0; i < sizeof(procs)/sizeof(procs[0]) && count < capacity; i++) {
      copy_name_long(entries[count].name, procs[i]);
      entries[count].size = (strcmp(procs[i], "kernel") == 0) ? 0 : 1;
      entries[count].attributes = (strcmp(procs[i], "kernel") == 0) ? FS_ATTRIBUTE_DIRECTORY : 0;
      entries[count].reserved[0] = entries[count].reserved[1] = entries[count].reserved[2] = 0;
      count++;
    }
  } else if (strcmp(path, "/sys") == 0) {
    const char *syss[] = {"class", "devices", "kernel"};
    for (uint32_t i = 0; i < sizeof(syss)/sizeof(syss[0]) && count < capacity; i++) {
      copy_name_long(entries[count].name, syss[i]);
      entries[count].size = 0;
      entries[count].attributes = FS_ATTRIBUTE_DIRECTORY;
      entries[count].reserved[0] = entries[count].reserved[1] = entries[count].reserved[2] = 0;
      count++;
    }
  } else if (strcmp(path, "/proc/kernel") == 0) {
    const char *kerns[] = {"version", "panic", "init", "abi"};
    for (uint32_t i = 0; i < sizeof(kerns)/sizeof(kerns[0]) && count < capacity; i++) {
      copy_name_long(entries[count].name, kerns[i]);
      entries[count].size = 1;
      entries[count].attributes = 0;
      entries[count].reserved[0] = entries[count].reserved[1] = entries[count].reserved[2] = 0;
      count++;
    }
  } else if (strcmp(path, "/sys/class") == 0) {
    if (count < capacity) {
      copy_name_long(entries[count].name, "block");
      entries[count].size = 0;
      entries[count].attributes = FS_ATTRIBUTE_DIRECTORY;
      entries[count].reserved[0] = entries[count].reserved[1] = entries[count].reserved[2] = 0;
      count++;
    }
  } else if (strcmp(path, "/sys/class/block") == 0) {
  } else if (strcmp(path, "/sys/devices") == 0) {
  } else if (strcmp(path, "/sys/kernel") == 0) {
    if (count < capacity) {
      copy_name_long(entries[count].name, "version");
      entries[count].size = 1;
      entries[count].attributes = 0;
      entries[count].reserved[0] = entries[count].reserved[1] = entries[count].reserved[2] = 0;
      count++;
    }
  }
  return (int32_t)count;
}

static uint32_t handles_capacity;
static uint64_t handles_phys;
static uint64_t handles_pages;

static bool handles_ensure(uint32_t want) {
  if (want < handles_capacity)
    return true;
  uint32_t grown = handles_capacity ? handles_capacity * 2 : VFS_HANDLE_INITIAL;
  if (grown < want)
    grown = want;
  if (grown > (1u << 20))
    return false;
  uint64_t pages = ((uint64_t)grown * sizeof(struct vfs_handle) + 4095) / 4096;
  uint64_t phys = pmm_allocate_contiguous(pages);
  if (!phys)
    return false;
  struct vfs_handle *tab = (struct vfs_handle *)pmm_physical_to_virtual(phys);
  memset(tab, 0, (size_t)(pages * 4096));
  if (handles && handles_capacity)
    memcpy(tab, handles, (size_t)handles_capacity * sizeof(struct vfs_handle));
  if (handles_phys)
    pmm_free_contiguous(handles_phys, handles_pages);
  handles = tab;
  handles_capacity = (uint32_t)((pages * 4096) / sizeof(struct vfs_handle));
  handles_phys = phys;
  handles_pages = pages;
  return true;
}

static int32_t allocate_handle(void) {
  for (uint32_t i = 0; i < handles_capacity; i++)
    if (handles[i].type == VFS_HANDLE_NONE)
      return (int32_t)i;
  uint32_t at = handles_capacity;
  if (!handles_ensure(at + 1))
    return FS_ERROR_NO_SPACE;
  return (int32_t)at;
}

static struct vfs_handle *get_handle(int32_t descriptor) {
  int32_t idx = descriptor - VFS_FD_BASE;
  if (idx < 0 || (uint32_t)idx >= handles_capacity)
    return 0;
  if (!handles || handles[idx].type == VFS_HANDLE_NONE)
    return 0;
  return &handles[idx];
}

const char *vfs_fs_type_name(uint8_t fs_type) {
  MUTEX_SCOPE(&vfs_mutex);
  if (fs_type == VFS_FS_EXT2)
    return "ext2";
  if (fs_type == VFS_FS_INITRAMFS)
    return "initramfs";
  return "unknown";
}

uint8_t vfs_root_fs_type(void) {
  MUTEX_SCOPE(&vfs_mutex);
  return vfs_active_fs;
}

void vfs_set_active_fs(uint8_t fs_type) {
  MUTEX_SCOPE(&vfs_mutex);
  vfs_active_fs = fs_type;
}

bool vfs_mount_root(void) {
  MUTEX_SCOPE(&vfs_mutex);
  return vfs_mount_root_with_fs(VFS_FS_AUTO);
}

bool vfs_mount_initramfs(void) {
  MUTEX_SCOPE(&vfs_mutex);
  if (!initramfs_mount())
    return false;
  vfs_active_fs = VFS_FS_INITRAMFS;
  return true;
}

bool vfs_mount_root_with_fs(uint8_t fs_type) {
  MUTEX_SCOPE(&vfs_mutex);
  if (fs_type == VFS_FS_INITRAMFS)
    return false;
  if (fs_type == VFS_FS_EXT2 || fs_type == VFS_FS_AUTO) {
    if (ext2_init()) {
      vfs_active_fs = VFS_FS_EXT2;
      return true;
    }
  }
  return false;
}

bool vfs_is_root_mounted(void) {
  MUTEX_SCOPE(&vfs_mutex);
  if (vfs_active_fs == VFS_FS_EXT2)
    return ext2_is_mounted();
  if (vfs_active_fs == VFS_FS_INITRAMFS)
    return initramfs_is_mounted();
  return false;
}

const char *vfs_root_device_name(void) {
  MUTEX_SCOPE(&vfs_mutex);
  if (vfs_active_fs == VFS_FS_EXT2)
    return ext2_device_name();
  if (vfs_active_fs == VFS_FS_INITRAMFS)
    return "initramfs";
  return 0;
}

static bool is_klog_path(const char *path) {
  return path &&
         (strcmp(path, "/var/log/kernel.log") == 0 ||
          strcmp(path, "/var/log/dmesg") == 0);
}

int32_t vfs_open(const char *path) {
  MUTEX_SCOPE(&vfs_mutex);
  if (!path || !path[0])
    return FS_ERROR_INVALID;
  
  uint8_t vkind = virtual_kind_from_path(path);
  if (vkind != VIRT_NONE) {
    int32_t idx = allocate_handle();
    if (idx < 0) return idx;
    struct vfs_handle *h = &handles[idx];
    h->type = VFS_HANDLE_VIRTUAL;
    h->virt_kind = vkind;
    h->backend_descriptor = -1;
    h->data = h->virt_buf;
    h->virt_size = 0;
    virtual_file_generate(vkind, h->virt_buf, &h->virt_size);
    h->size = h->virt_size;
    h->position = 0;
    return VFS_FD_BASE + idx;
  }

  if (is_virtual_dir(path)) {
    int32_t idx = allocate_handle();
    if (idx < 0) return idx;
    struct vfs_handle *h = &handles[idx];
    h->type = VFS_HANDLE_VIRTUAL;
    h->virt_kind = VIRT_NONE; // directory marker
    h->backend_descriptor = -1;
    h->data = 0;
    h->size = 0;
    h->position = 0;
    return VFS_FD_BASE + idx;
  }

  const struct kernel_file *kf = find_kernel_file(path);
  int32_t idx = allocate_handle();
  if (idx < 0)
    return idx;
  struct vfs_handle *h = &handles[idx];
  if (kf) {
    h->type = VFS_HANDLE_KERNEL_FILE;
    h->backend_descriptor = -1;
    h->data = kf->content;
    h->size = (uint32_t)strlen(kf->content);
    h->position = 0;
    return VFS_FD_BASE + idx;
  }
  if (is_klog_path(path)) {
    h->type = VFS_HANDLE_KLOG;
    h->backend_descriptor = -1;
    h->data = 0;
    h->size = 0;
    h->position = 0;
    h->klog_cursor = 0;
    uint64_t total = klog_total_bytes();
    h->klog_cursor = total > (8 * 1024 * 1024) ? total - 8 * 1024 * 1024 : 0;
    h->klog_data_lost = false;
    return VFS_FD_BASE + idx;
  }
  int32_t be = -1;
  {
    const void *data = 0;
    uint32_t size = 0;
    if (initramfs_find(path, &data, &size)) {
      h->type = VFS_HANDLE_INITRAMFS;
      h->backend_descriptor = -1;
      h->data = data;
      h->size = size;
      h->position = 0;
      return VFS_FD_BASE + idx;
    }
  }
  if (vfs_active_fs == VFS_FS_EXT2)
    be = ext2_open(path);
  else
    return FS_ERROR_UNSUPPORTED;
  if (be < 0) {
    memset(h, 0, sizeof(*h));
    return be;
  }
  h->type = VFS_HANDLE_EXT2;
  h->backend_descriptor = be;
  h->data = 0;
  h->size = 0;
  h->position = 0;
  return VFS_FD_BASE + idx;
}

int32_t vfs_read(int32_t descriptor, void *buffer, uint32_t count) {
  MUTEX_SCOPE(&vfs_mutex);
  if (!buffer && count)
    return FS_ERROR_INVALID;
  struct vfs_handle *h = get_handle(descriptor);
  if (!h)
    return FS_ERROR_INVALID;
  if (h->type == VFS_HANDLE_EXT2)
    return ext2_read(h->backend_descriptor, buffer, count);
  if (h->type == VFS_HANDLE_KLOG) {
    uint32_t a = klog_read_since(&h->klog_cursor, (char *)buffer, count,
                                 &h->klog_data_lost);
    return (int32_t)a;
  }
  if (h->type == VFS_HANDLE_VIRTUAL && h->virt_kind != VIRT_NONE) {
    // For /dev/zero, return zeros; /dev/null returns 0 (EOF); /dev/random returns random
    if (h->virt_kind == VIRT_DEV_ZERO) {
      if (count > VIRT_BUF_SIZE) count = VIRT_BUF_SIZE;
      memset(buffer, 0, count);
      return (int32_t)count;
    }
    if (h->virt_kind == VIRT_DEV_NULL || h->virt_kind == VIRT_DEV_FULL) {
      return 0; // EOF for null, ENOSPC for full (handled by write)
    }
    if (h->virt_kind == VIRT_DEV_RANDOM || h->virt_kind == VIRT_DEV_URANDOM) {
      // Simple pseudo-random
      uint8_t *dst = buffer;
      for (uint32_t i = 0; i < count; i++) {
        static uint32_t seed = 0x12345678;
        seed = seed * 1103515245 + 12345;
        dst[i] = (uint8_t)(seed >> 16);
      }
      return (int32_t)count;
    }
    // For generated content files, use the pre-generated buffer
    if (h->position >= h->virt_size)
      return 0;
    uint32_t rem = h->virt_size - h->position;
    uint32_t amt = count < rem ? count : rem;
    if (amt)
      memcpy(buffer, h->virt_buf + h->position, amt);
    h->position += amt;
    return (int32_t)amt;
  }
  if (h->type != VFS_HANDLE_KERNEL_FILE && h->type != VFS_HANDLE_INITRAMFS)
    return FS_ERROR_INVALID;
  if (h->position >= h->size)
    return 0;
  uint32_t rem = h->size - h->position;
  uint32_t amt = count < rem ? count : rem;
  if (amt)
    memcpy(buffer, h->data + h->position, amt);
  h->position += amt;
  return (int32_t)amt;
}

int32_t vfs_close(int32_t descriptor) {
  MUTEX_SCOPE(&vfs_mutex);
  struct vfs_handle *h = get_handle(descriptor);
  if (!h)
    return FS_ERROR_INVALID;
  int32_t r = 0;
  if (h->type == VFS_HANDLE_EXT2)
    r = ext2_close(h->backend_descriptor);
  memset(h, 0, sizeof(*h));
  return r;
}

int64_t vfs_seek(int32_t descriptor, int64_t offset, uint32_t whence) {
  MUTEX_SCOPE(&vfs_mutex);
  if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END)
    return FS_ERROR_INVALID;
  struct vfs_handle *h = get_handle(descriptor);
  if (!h)
    return FS_ERROR_INVALID;
  if (h->type == VFS_HANDLE_EXT2)
    return ext2_seek(h->backend_descriptor, offset, whence);
  if (h->type == VFS_HANDLE_FAT32)
    return fat32_seek(h->backend_descriptor, offset, whence);
  if (h->type == VFS_HANDLE_KLOG) {
    uint64_t total = klog_total_bytes();
    int64_t base = 0;
    if (whence == SEEK_CUR)
      base = (int64_t)h->klog_cursor;
    else if (whence == SEEK_END)
      base = (int64_t)total;
    int64_t target = base + offset;
    if (target < 0)
      target = 0;
    if ((uint64_t)target > total)
      target = (int64_t)total;
    h->klog_cursor = (uint64_t)target;
    return target;
  }
  if (h->type != VFS_HANDLE_KERNEL_FILE && h->type != VFS_HANDLE_INITRAMFS)
    return FS_ERROR_INVALID;
  int64_t base = 0;
  if (whence == SEEK_CUR)
    base = (int64_t)h->position;
  else if (whence == SEEK_END)
    base = (int64_t)h->size;
  int64_t target = base + offset;
  if (target < 0)
    return FS_ERROR_INVALID;
  h->position = (uint32_t)(target > 0xFFFFFFFFLL ? 0xFFFFFFFFLL : target);
  return (int64_t)h->position;
}

int32_t vfs_stat(const char *path, struct file_stat_info *out) {
  MUTEX_SCOPE(&vfs_mutex);
  if (!path || !path[0] || !out)
    return FS_ERROR_INVALID;
  
  uint8_t vkind = virtual_kind_from_path(path);
  if (vkind != VIRT_NONE) {
    out->size = 0;
    out->is_directory = 0;
    out->reserved = 0;
    return 0;
  }
  
  if (is_virtual_dir(path)) {
    out->size = 0;
    out->is_directory = 1;
    out->reserved = 0;
    return 0;
  }

  const struct kernel_file *kf = find_kernel_file(path);
  if (kf) {
    out->size = (uint64_t)strlen(kf->content);
    out->is_directory = 0;
    out->reserved = 0;
    return 0;
  }
  if (path_equals(path, "/kernel")) {
    out->size = 0;
    out->is_directory = 1;
    out->reserved = 0;
    return 0;
  }
  if (is_klog_path(path)) {
    out->size = klog_total_bytes();
    out->is_directory = 0;
    out->reserved = 0;
    return 0;
  }
  if (initramfs_is_mounted()) {
    const void *data = 0;
    uint32_t size = 0;
    if (initramfs_find(path, &data, &size)) {
      out->size = size;
      out->is_directory = 0;
      out->reserved = 0;
      return 0;
    }
    if (vfs_active_fs == VFS_FS_INITRAMFS)
      return FS_ERROR_NOT_FOUND;
  }
  if (vfs_active_fs == VFS_FS_EXT2) {
    uint32_t ino;
    int32_t st = ext2_dir_resolve(path, &ino);
    if (st < 0)
      return st;
    uint8_t ib[256];
    if (!ext2_inode_read(ino, ib))
      return FS_ERROR_IO;
    out->size = ext2_read_u32(ib + 4);
    out->is_directory = ((ext2_read_u16(ib) & 0xF000) == EXT2_S_IFDIR) ? 1 : 0;
    out->reserved = 0;
    return 0;
  }
  return FS_ERROR_UNSUPPORTED;
}

int32_t vfs_delete(const char *path) {
  MUTEX_SCOPE(&vfs_mutex);
  if (find_kernel_file(path) || path_equals(path, "/kernel") ||
      is_virtual_dir(path) || virtual_kind_from_path(path) != VIRT_NONE)
    return FS_ERROR_READ_ONLY;
  if (vfs_active_fs == VFS_FS_EXT2)
    return ext2_delete(path);
  return FS_ERROR_UNSUPPORTED;
}

int32_t vfs_rename(const char *path, const char *new_name) {
  MUTEX_SCOPE(&vfs_mutex);
  if (find_kernel_file(path) || path_equals(path, "/kernel") ||
      is_virtual_dir(path) || virtual_kind_from_path(path) != VIRT_NONE)
    return FS_ERROR_READ_ONLY;
  if (vfs_active_fs == VFS_FS_EXT2)
    return ext2_rename(path, new_name);
  return FS_ERROR_UNSUPPORTED;
}

int32_t vfs_move(const char *path, const char *dest) {
  MUTEX_SCOPE(&vfs_mutex);
  if (find_kernel_file(path) || path_equals(path, "/kernel") ||
      is_virtual_dir(path) || virtual_kind_from_path(path) != VIRT_NONE)
    return FS_ERROR_READ_ONLY;
  if (vfs_active_fs == VFS_FS_EXT2)
    return ext2_move(path, dest);
  return FS_ERROR_UNSUPPORTED;
}

int32_t vfs_list(const char *path, struct fs_directory_entry *entries,
                 uint32_t capacity) {
  MUTEX_SCOPE(&vfs_mutex);
  if (!path || !entries || capacity == 0)
    return FS_ERROR_INVALID;
  if (is_virtual_dir(path)) {
    return virtual_dir_list(path, entries, capacity, false);
  }
  if (path_equals(path, "/")) {
    int32_t be = 0;
    if (vfs_active_fs == VFS_FS_INITRAMFS)
      be = initramfs_list(path, entries, capacity);
    else if (vfs_active_fs == VFS_FS_EXT2)
      be = ext2_list(path, entries, capacity);
    else
      be = fat32_list(path, entries, capacity);
    uint32_t count = be > 0 ? (uint32_t)be : 0;
    if (count < capacity) {
      copy_name(entries[count].name, "kernel");
      entries[count].size = 0;
      entries[count].attributes = FS_ATTRIBUTE_DIRECTORY;
      count++;
    }
    if (count < capacity) {
      copy_name(entries[count].name, "dev");
      entries[count].size = 0;
      entries[count].attributes = FS_ATTRIBUTE_DIRECTORY;
      count++;
    }
    if (count < capacity) {
      copy_name(entries[count].name, "proc");
      entries[count].size = 0;
      entries[count].attributes = FS_ATTRIBUTE_DIRECTORY;
      count++;
    }
    if (count < capacity) {
      copy_name(entries[count].name, "sys");
      entries[count].size = 0;
      entries[count].attributes = FS_ATTRIBUTE_DIRECTORY;
      count++;
    }
    if (count < capacity) {
      bool has = false;
      for (uint32_t i = 0; i < count; i++)
        if (strcmp(entries[i].name, "dmesg.txt") == 0)
          has = true;
      if (!has) {
        copy_name(entries[count].name, "dmesg.txt");
        entries[count].size = (uint32_t)klog_total_bytes();
        if (entries[count].size > 8 * 1024 * 1024)
          entries[count].size = 8 * 1024 * 1024;
        entries[count].attributes = 0;
        count++;
      }
    }
    if (count < capacity) {
      bool has = false;
      for (uint32_t i = 0; i < count; i++)
        if (strcmp(entries[i].name, "kernel.log") == 0)
          has = true;
      if (!has) {
        copy_name(entries[count].name, "kernel.log");
        entries[count].size = (uint32_t)klog_total_bytes();
        if (entries[count].size > 8 * 1024 * 1024)
          entries[count].size = 8 * 1024 * 1024;
        entries[count].attributes = 0;
        count++;
      }
    }
    return (int32_t)count;
  }
  if (path_equals(path, "/kernel")) {
    uint32_t count = sizeof(kernel_files) / sizeof(kernel_files[0]);
    if (count > capacity)
      count = capacity;
    for (uint32_t i = 0; i < count; i++) {
      copy_name(entries[i].name, kernel_files[i].name);
      entries[i].size = (uint32_t)strlen(kernel_files[i].content);
      entries[i].attributes = 0;
    }
    return (int32_t)count;
  }
  if (vfs_active_fs == VFS_FS_INITRAMFS)
    return initramfs_list(path, entries, capacity);
  if (vfs_active_fs == VFS_FS_EXT2)
    return ext2_list(path, entries, capacity);
  return FS_ERROR_UNSUPPORTED;
}

static void copy_name_long(char destination[FS_LONG_NAME_CAPACITY],
                           const char *source) {
  uint32_t i = 0;
  while (source[i] && i + 1 < FS_LONG_NAME_CAPACITY) {
    destination[i] = source[i];
    i++;
  }
  destination[i] = '\0';
}

int32_t vfs_list_long(const char *path, struct fs_directory_entry_long *entries,
                      uint32_t capacity) {
  MUTEX_SCOPE(&vfs_mutex);
  if (!path || !entries || capacity == 0)
    return FS_ERROR_INVALID;
  if (is_virtual_dir(path)) {
    return virtual_dir_list_long(path, entries, capacity);
  }
  if (path_equals(path, "/")) {
    int32_t be = 0;
    if (vfs_active_fs == VFS_FS_INITRAMFS)
      be = initramfs_list_long(path, entries, capacity);
    else if (vfs_active_fs == VFS_FS_EXT2)
      be = ext2_list_long(path, entries, capacity);
    else
      be = fat32_list_long(path, entries, capacity);
    uint32_t count = be > 0 ? (uint32_t)be : 0;
    if (count < capacity) {
      copy_name_long(entries[count].name, "kernel");
      entries[count].size = 0;
      entries[count].attributes = FS_ATTRIBUTE_DIRECTORY;
      count++;
    }
    if (count < capacity) {
      copy_name_long(entries[count].name, "dev");
      entries[count].size = 0;
      entries[count].attributes = FS_ATTRIBUTE_DIRECTORY;
      count++;
    }
    if (count < capacity) {
      copy_name_long(entries[count].name, "proc");
      entries[count].size = 0;
      entries[count].attributes = FS_ATTRIBUTE_DIRECTORY;
      count++;
    }
    if (count < capacity) {
      copy_name_long(entries[count].name, "sys");
      entries[count].size = 0;
      entries[count].attributes = FS_ATTRIBUTE_DIRECTORY;
      count++;
    }
    if (count < capacity) {
      bool has = false;
      for (uint32_t i = 0; i < count; i++)
        if (strcmp(entries[i].name, "dmesg.txt") == 0)
          has = true;
      if (!has) {
        copy_name_long(entries[count].name, "dmesg.txt");
        entries[count].size = (uint32_t)klog_total_bytes();
        if (entries[count].size > 8 * 1024 * 1024)
          entries[count].size = 8 * 1024 * 1024;
        entries[count].attributes = 0;
        count++;
      }
    }
    if (count < capacity) {
      bool has = false;
      for (uint32_t i = 0; i < count; i++)
        if (strcmp(entries[i].name, "kernel.log") == 0)
          has = true;
      if (!has) {
        copy_name_long(entries[count].name, "kernel.log");
        entries[count].size = (uint32_t)klog_total_bytes();
        if (entries[count].size > 8 * 1024 * 1024)
          entries[count].size = 8 * 1024 * 1024;
        entries[count].attributes = 0;
        count++;
      }
    }
    return (int32_t)count;
  }
  if (path_equals(path, "/kernel")) {
    uint32_t count = sizeof(kernel_files) / sizeof(kernel_files[0]);
    if (count > capacity)
      count = capacity;
    for (uint32_t i = 0; i < count; i++) {
      copy_name_long(entries[i].name, kernel_files[i].name);
      entries[i].size = (uint32_t)strlen(kernel_files[i].content);
      entries[i].attributes = 0;
    }
    return (int32_t)count;
  }
  if (vfs_active_fs == VFS_FS_INITRAMFS)
    return initramfs_list_long(path, entries, capacity);
  if (vfs_active_fs == VFS_FS_EXT2)
    return ext2_list_long(path, entries, capacity);
  return FS_ERROR_UNSUPPORTED;
}

int32_t vfs_create_file(const char *path) {
  MUTEX_SCOPE(&vfs_mutex);
  if (!path || path_equals(path, "/kernel"))
    return FS_ERROR_INVALID;
  if (find_kernel_file(path))
    return FS_ERROR_READ_ONLY;
  if (is_klog_path(path))
    return 0;
  if (vfs_active_fs == VFS_FS_INITRAMFS)
    return FS_ERROR_READ_ONLY;
  if (vfs_active_fs == VFS_FS_EXT2)
    return ext2_create_file(path);
  return FS_ERROR_UNSUPPORTED;
}

static uint32_t raw_log_base_lba;
static uint32_t raw_log_next_sector;
static bool raw_log_inited;
static int32_t raw_log_write(const void *buffer, uint32_t count);
static int32_t raw_log_truncate(void);

int32_t vfs_write_file(const char *path, const void *buffer, uint32_t count) {
  MUTEX_SCOPE(&vfs_mutex);
  if (find_kernel_file(path) || path_equals(path, "/kernel"))
    return FS_ERROR_READ_ONLY;
  
  // Handle virtual devices
  if (strcmp(path, "/dev/null") == 0) {
    return (int32_t)count; // discard, pretend success
  }
  if (strcmp(path, "/dev/zero") == 0 || strcmp(path, "/dev/full") == 0) {
    return FS_ERROR_NO_SPACE; // /dev/zero can't be written, /dev/full always ENOSPC
  }
  if (strcmp(path, "/dev/random") == 0 || strcmp(path, "/dev/urandom") == 0) {
    return (int32_t)count; // discard
  }
  
  if (is_klog_path(path)) {
    if (count == 0)
      raw_log_truncate();
    int32_t r = ext2_write_file(path, buffer, count);
    if (r >= 0) {
      (void)raw_log_write(buffer, count);
      return r;
    }
    int32_t raw = raw_log_write(buffer, count);
    if (raw >= 0)
      return (int32_t)count;
    return (int32_t)count;
  }
  if (vfs_active_fs == VFS_FS_INITRAMFS)
    return FS_ERROR_READ_ONLY;
  if (vfs_active_fs == VFS_FS_EXT2) {
    int32_t ext_result = ext2_write_file(path, buffer, count);
    if (ext_result >= 0)
      block_device_flush();
    return ext_result;
  }
  return FS_ERROR_UNSUPPORTED;
}

int32_t vfs_append_file(const char *path, const void *buffer, uint32_t count) {
  MUTEX_SCOPE(&vfs_mutex);
  if (find_kernel_file(path) || path_equals(path, "/kernel"))
    return FS_ERROR_READ_ONLY;
  
  // Handle virtual devices
  if (strcmp(path, "/dev/null") == 0) {
    return (int32_t)count;
  }
  if (strcmp(path, "/dev/zero") == 0 || strcmp(path, "/dev/full") == 0) {
    return FS_ERROR_NO_SPACE;
  }
  if (strcmp(path, "/dev/random") == 0 || strcmp(path, "/dev/urandom") == 0) {
    return (int32_t)count;
  }
  
  if (is_klog_path(path)) {
    int32_t r = ext2_append_file(path, buffer, count);
    if (r < 0 && !vfs_is_root_mounted())
      r = ext2_write_file(path, buffer, count);
    if (r >= 0) {
      (void)raw_log_write(buffer, count);
      return r;
    }
    int32_t raw = raw_log_write(buffer, count);
    if (raw >= 0)
      return (int32_t)count;
    return (int32_t)count;
  }
  if (vfs_active_fs == VFS_FS_EXT2) {
    int32_t ext_result = ext2_append_file(path, buffer, count);
    if (ext_result >= 0)
      block_device_flush();
    return ext_result;
  }
  return FS_ERROR_UNSUPPORTED;
}

int32_t vfs_create_directory(const char *path) {
  MUTEX_SCOPE(&vfs_mutex);
  if (!path || path_equals(path, "/kernel"))
    return FS_ERROR_INVALID;
  if (find_kernel_file(path))
    return FS_ERROR_READ_ONLY;
  if (vfs_active_fs == VFS_FS_EXT2)
    return ext2_create_directory(path);
  return fat32_create_directory(path);
}

int32_t vfs_format_device(const char *device_name,
                          const char *serial_confirmation,
                          const char *erase_confirmation) {
  MUTEX_SCOPE(&vfs_mutex);
  return vfs_format_device_ex(device_name, serial_confirmation,
                              erase_confirmation, VFS_FS_EXT2);
}

int32_t vfs_format_device_force(const char *device_name,
                                const char *serial_confirmation) {
  MUTEX_SCOPE(&vfs_mutex);
  return vfs_format_device_ex(device_name, serial_confirmation, "ERASE",
                              VFS_FS_EXT2);
}

int32_t vfs_format_device_ex(const char *device_name,
                             const char *serial_confirmation,
                             const char *erase_confirmation, uint8_t fs_type) {
  MUTEX_SCOPE(&vfs_mutex);
  if (!device_name || !device_name[0])
    return FS_ERROR_INVALID;
  if (fs_type == VFS_FS_EXT2)
    return ext2_format_device(device_name, serial_confirmation,
                              erase_confirmation);
  if (fs_type == VFS_FS_FAT32)
    return fat32_format_device(device_name, serial_confirmation,
                               erase_confirmation);
  return FS_ERROR_UNSUPPORTED;
}

int32_t vfs_format_uefi_device(const char *device_name,
                               const char *serial_confirmation) {
  MUTEX_SCOPE(&vfs_mutex);
  return fat32_format_uefi_device(device_name, serial_confirmation);
}

int32_t vfs_format_uefi_device_progress(const char *device_name,
                                        const char *serial_confirmation,
                                        fat32_progress_callback callback) {
  MUTEX_SCOPE(&vfs_mutex);
  return fat32_format_uefi_device_progress(device_name, serial_confirmation,
                                           callback);
}

static bool raw_log_init(void) {
  if (raw_log_inited)
    return true;
  uint32_t n = block_device_count();
  if (n == 0)
    return false;
  struct storage_device_info info;
  bool found = false;
  for (uint32_t i = 0; i < n; i++)
    if (block_device_get_info(i, &info) && info.selected) {
      found = true;
      break;
    }
  if (!found)
    if (!block_device_get_info(0, &info))
      return false;
  if (info.sector_count < 4096)
    return false;
  raw_log_base_lba = 1024;
  if (raw_log_base_lba + 2048 > info.sector_count)
    raw_log_base_lba = info.sector_count - 2048;
  raw_log_next_sector = 1;
  raw_log_inited = true;
  uint8_t hdr[512];
  hdr[0] = 'K';
  hdr[1] = 'L';
  hdr[2] = 'O';
  hdr[3] = 'G';
  hdr[4] = 0;
  hdr[5] = 0;
  hdr[6] = 0;
  hdr[7] = 0;
  for (uint32_t i = 8; i < 512; i++)
    hdr[i] = 0;
  (void)block_device_write(raw_log_base_lba, hdr);
  return true;
}

static int32_t raw_log_truncate(void) {
  if (!raw_log_init())
    return FS_ERROR_INVALID;
  raw_log_next_sector = 1;
  uint8_t hdr[512];
  hdr[0] = 'K';
  hdr[1] = 'L';
  hdr[2] = 'O';
  hdr[3] = 'G';
  for (uint32_t i = 4; i < 512; i++)
    hdr[i] = 0;
  if (!block_device_write(raw_log_base_lba, hdr))
    return FS_ERROR_IO;
  return 0;
}

static int32_t raw_log_write(const void *buffer, uint32_t count) {
  if (!buffer && count)
    return FS_ERROR_INVALID;
  if (count == 0)
    return 0;
  if (!raw_log_init())
    return FS_ERROR_INVALID;
  const uint8_t *src = (const uint8_t *)buffer;
  uint32_t written = 0;
  uint8_t sector[512];
  while (count > 0) {
    if (raw_log_next_sector >= 2048)
      break;
    uint32_t to_copy = count > 512 ? 512 : count;
    memset(sector, 0, 512);
    memcpy(sector, src, to_copy);
    if (!block_device_write(raw_log_base_lba + raw_log_next_sector, sector))
      break;
    raw_log_next_sector++;
    src += to_copy;
    count -= to_copy;
    written += to_copy;
    if (count == 0) {
      uint8_t hdr[512];
      if (block_device_read(raw_log_base_lba, hdr)) {
        hdr[4] = (uint8_t)(raw_log_next_sector & 0xFF);
        hdr[5] = (uint8_t)((raw_log_next_sector >> 8) & 0xFF);
        hdr[6] = (uint8_t)((raw_log_next_sector >> 16) & 0xFF);
        hdr[7] = (uint8_t)((raw_log_next_sector >> 24) & 0xFF);
        (void)block_device_write(raw_log_base_lba, hdr);
      }
    }
  }
  return written ? (int32_t)written : FS_ERROR_IO;
}

int32_t vfs_save_klog_to_device(const char *device, const char *path) {
  MUTEX_SCOPE(&vfs_mutex);
  if (!device || !device[0] || !path || !path[0])
    return FS_ERROR_INVALID;
  int32_t dev_idx = block_device_find(device);
  if (dev_idx < 0)
    return FS_ERROR_NOT_FOUND;
  struct storage_device_info info;
  if (!block_device_get_info((uint32_t)dev_idx, &info))
    return FS_ERROR_INVALID;
  if (!info.operational || !info.writable)
    return FS_ERROR_READ_ONLY;
  const char *prev_root = vfs_root_device_name();
  char prev_device[32] = {0};
  if (prev_root) {
    strncpy(prev_device, prev_root, sizeof(prev_device) - 1);
    prev_device[sizeof(prev_device) - 1] = '\0';
  }
  bool mounted = false;
  if (prev_root && strcmp(prev_root, device) == 0)
    mounted = vfs_is_root_mounted();
  else {
    if (block_device_select((uint32_t)dev_idx)) {
      if (ext2_mount_specific(device)) {
        vfs_active_fs = VFS_FS_EXT2;
        mounted = true;
      } else
        mounted = vfs_mount_root();
    }
  }
  if (!mounted)
    return FS_ERROR_IO;
  uint64_t total = klog_total_bytes();
  uint64_t cursor = total > (8 * 1024 * 1024) ? total - 8 * 1024 * 1024 : 0;
  static char log_buffer[64 * 1024];
  uint64_t write_cursor = cursor;
  (void)vfs_delete(path);
  int32_t fd = vfs_open(path);
  if (fd < 0) {
    if (vfs_create_file(path) < 0) {
      if (prev_root && strcmp(prev_root, device) != 0 && prev_device[0]) {
        int32_t prev_idx = block_device_find(prev_device);
        if (prev_idx >= 0) {
          block_device_select((uint32_t)prev_idx);
          vfs_mount_root();
        }
      }
      return fd;
    }
    fd = vfs_open(path);
    if (fd < 0)
      return fd;
  }
  vfs_close(fd);
  uint32_t total_written = 0;
  bool data_lost = false;
  while (write_cursor < total) {
    uint32_t to_read = sizeof(log_buffer);
    if (write_cursor + to_read > total)
      to_read = (uint32_t)(total - write_cursor);
    uint32_t got =
        klog_read_since(&write_cursor, log_buffer, to_read, &data_lost);
    if (got == 0)
      break;
    int32_t wr = vfs_append_file(path, log_buffer, got);
    if (wr < 0) {
      wr = vfs_write_file(path, log_buffer, got);
      if (wr < 0)
        break;
    }
    total_written += got;
    if (data_lost)
      break;
  }
  if (prev_root && strcmp(prev_root, device) != 0 && prev_device[0]) {
    int32_t prev_idx = block_device_find(prev_device);
    if (prev_idx >= 0) {
      block_device_select((uint32_t)prev_idx);
      vfs_mount_root();
    }
  }
  if (total_written == 0)
    return FS_ERROR_IO;
  return (int32_t)total_written;
}

int32_t vfs_ext2_stat(const char *path, struct ext2_stat_info *out) {
  MUTEX_SCOPE(&vfs_mutex);
  if (vfs_active_fs != VFS_FS_EXT2)
    return FS_ERROR_UNSUPPORTED;
  return ext2_stat_path(path, out);
}
int32_t vfs_ext2_inode(uint32_t ino, struct ext2_stat_info *out) {
  MUTEX_SCOPE(&vfs_mutex);
  if (vfs_active_fs != VFS_FS_EXT2)
    return FS_ERROR_UNSUPPORTED;
  return ext2_stat_ino(ino, out);
}
int32_t vfs_ext2_super(struct ext2_super_info *out) {
  MUTEX_SCOPE(&vfs_mutex);
  if (vfs_active_fs != VFS_FS_EXT2)
    return FS_ERROR_UNSUPPORTED;
  return ext2_super_info(out);
}
int32_t vfs_ext2_blocks(const char *path, struct ext2_blocks_info *out) {
  MUTEX_SCOPE(&vfs_mutex);
  if (vfs_active_fs != VFS_FS_EXT2)
    return FS_ERROR_UNSUPPORTED;
  return ext2_file_blocks(path, out);
}
int32_t vfs_ext2_inode_blocks(uint32_t ino, struct ext2_blocks_info *out) {
  MUTEX_SCOPE(&vfs_mutex);
  if (vfs_active_fs != VFS_FS_EXT2)
    return FS_ERROR_UNSUPPORTED;
  return ext2_inode_blocks(ino, out);
}
