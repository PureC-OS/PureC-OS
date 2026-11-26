#include <arch/arch.h>
#include <io/io_sched.h>
#include <io/storage/block_device.h>
#include <io/storage/storage_probe.h>
#include <fs/install_accounts.h>
#include <fs/vfs.h>
#include <fs/vfs_fat32.h>
#include <lib/string.h>
#include <mm/pmm.h>
#include <kernel/diagnostics/klog.h>
#include <kernel/process/process.h>
#include <kernel/process/scheduler.h>
#include "../common.h"

#define INSTALL_WORKER_PRIORITY 3

struct install_status install_job;
static struct install_log install_history;
static char install_device[STORAGE_DEVICE_NAME_CAPACITY];
static char install_serial[STORAGE_SERIAL_CAPACITY];
volatile int install_worker_tid = -1;

bool install_job_running(void)
{
  return install_job.state == 1;
}

static void install_progress(uint32_t progress, const char *stage)
{
  uint64_t flags;
  flags = arch_irq_save();
  install_job.progress = progress;
  if (stage)
    strncpy(install_job.stage, stage, sizeof(install_job.stage) - 1);
  install_job.stage[sizeof(install_job.stage) - 1] = '\0';
  if (stage)
  {
    if (install_history.count == INSTALL_LOG_CAPACITY)
    {
      for (uint32_t index = 1; index < INSTALL_LOG_CAPACITY; index++)
        install_history.entries[index - 1] = install_history.entries[index];
      install_history.count--;
    }
    struct install_log_entry *entry =
        &install_history.entries[install_history.count++];
    entry->progress = progress;
    strncpy(entry->stage, stage, sizeof(entry->stage) - 1);
    entry->stage[sizeof(entry->stage) - 1] = '\0';
  }
  if (flags & (1ULL << 9))
    arch_irq_enable();
}

static void install_worker(void *argument)
{
  (void)argument;
  __atomic_store_n(&install_worker_tid, scheduler_current_tid(), __ATOMIC_RELEASE);
  filesystem_syscall_lock();
  block_device_begin_exclusive_io();
  io_bulk_begin();
  int32_t result = installer_install_device(install_device, install_serial,
                                            install_progress);
  io_bulk_end();
  block_device_end_exclusive_io();
  if (result >= 0)
  {
    int32_t target = block_device_find(install_device);
    if (target < 0 || !vfs_fat32_mount_boot_device((uint32_t)target))
      klog(KLOG_WARN, "install: new EFI partition not mounted on /boot");
  }
  filesystem_syscall_unlock();
  __atomic_store_n(&install_worker_tid, -1, __ATOMIC_RELEASE);
  install_job.result = result;
  if (result < 0)
  {
    install_job.state = 3;
    char failed_stage[INSTALL_STAGE_CAPACITY];
    const char *prefix = "Failed: ";
    uint32_t length = 0;
    while (prefix[length] && length + 1 < sizeof(failed_stage))
    {
      failed_stage[length] = prefix[length];
      length++;
    }
    for (uint32_t index = 0;
         install_job.stage[index] && length + 1 < sizeof(failed_stage); index++)
      failed_stage[length++] = install_job.stage[index];
    failed_stage[length] = '\0';
    install_progress(install_job.progress, failed_stage);
  }
  else
  {
    install_job.state = 2;
    install_progress(92, "Disk layout complete");
  }
}

void install_report_ui_crash(int32_t status)
{
  if (install_job.state != 0)
    return;
  memset(&install_job, 0, sizeof(install_job));
  memset(&install_history, 0, sizeof(install_history));
  install_job.state = 3;
  install_job.result = status;
  install_progress(0, "Installer UI crashed");
}

long sys_disk_list(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (!writable((void *)(uintptr_t)a1,
                (uint64_t)(uint32_t)a2 * sizeof(struct storage_device_info)))
    return -1;
  return block_device_list((struct storage_device_info *)(uintptr_t)a1,
                           (uint32_t)a2);
}

long sys_disk_stats(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  struct disk_monitor_info *info = (struct disk_monitor_info *)(uintptr_t)a1;
  if (!writable(info, sizeof(*info)))
    return -1;
  memset(info, 0, sizeof(*info));
  info->device_count = block_device_count();
  for (uint32_t index = 0; index < info->device_count; index++)
  {
    struct storage_device_info device;
    if (!block_device_get_info(index, &device))
      continue;
    if (device.operational)
      info->operational_count++;
    info->total_bytes += device.sector_count * device.sector_size;
  }
  return 0;
}

long sys_disk_io(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (!privileged(PROCESS_CAP_STORAGE_ADMIN))
    return -KERR_PERM;
  if (!readable((const void *)(uintptr_t)a1, sizeof(struct disk_io_request)))
    return -KERR_FAULT;
  struct disk_io_request req = *(const struct disk_io_request *)(uintptr_t)a1;
  req.device[sizeof(req.device) - 1] = '\0';
  if (!req.count || req.count > DISK_IO_MAX_SECTORS || req.lba > 0xFFFFFFFFULL ||
      req.lba + req.count > 0x100000000ULL)
    return -KERR_INVAL;
  uint64_t bytes = (uint64_t)req.count * 512u;
  void *user = (void *)(uintptr_t)req.buffer;
  if (req.write ? !readable(user, bytes) : !writable(user, bytes))
    return -KERR_FAULT;
  int32_t index = block_device_find(req.device);
  struct storage_device_info info;
  if (index < 0 || !block_device_get_info((uint32_t)index, &info))
    return -KERR_NODEV;
  if (info.sector_size != 512 || req.lba + req.count > info.sector_count)
    return -KERR_INVAL;
  if (req.write && !info.writable)
    return -KERR_ACCES;
  uint64_t pages = (bytes + 4095) / 4096;
  uint64_t phys = pmm_allocate_contiguous(pages);
  if (!phys)
    return -KERR_NOMEM;
  uint8_t *bounce = (uint8_t *)pmm_physical_to_virtual(phys);
  if (req.write)
    memcpy(bounce, user, bytes);
  bool ok = block_device_raw_run((uint32_t)index, req.write != 0, (uint32_t)req.lba, bounce, req.count);
  if (ok && !req.write)
    memcpy(user, bounce, bytes);
  pmm_free_contiguous(phys, pages);
  return ok ? (int64_t)req.count : -KERR_IO;
}

long sys_storage_controllers(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (!writable((void *)(uintptr_t)a1,
                (uint64_t)(uint32_t)a2 *
                    sizeof(struct storage_controller_info)))
    return -1;
  return storage_controller_list(
      (struct storage_controller_info *)(uintptr_t)a1, (uint32_t)a2);
}

long sys_fs_format(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a4;
  (void)a5;
  (void)a6;
  if (!privileged(PROCESS_CAP_STORAGE_ADMIN))
    return -1;
  if (!readable_string((const char *)(uintptr_t)a1) ||
      !readable_string((const char *)(uintptr_t)a2) ||
      (a3 && !readable_string((const char *)(uintptr_t)a3)))
    return -1;
  if (!filesystem_syscall_acquire())
    return -1;
  int32_t result = vfs_format_device((const char *)(uintptr_t)a1,
                                     (const char *)(uintptr_t)a2,
                                     (const char *)(uintptr_t)a3);
  filesystem_syscall_unlock();
  return result;
}

long sys_fs_format_force(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (!privileged(PROCESS_CAP_STORAGE_ADMIN))
    return -1;
  if (!readable_string((const char *)(uintptr_t)a1) ||
      !readable_string((const char *)(uintptr_t)a2))
    return -1;
  if (!filesystem_syscall_acquire())
    return -1;
  int32_t result = vfs_format_device_force((const char *)(uintptr_t)a1,
                                           (const char *)(uintptr_t)a2);
  filesystem_syscall_unlock();
  return result;
}

long sys_fs_format_custom(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (!privileged(PROCESS_CAP_STORAGE_ADMIN))
    return -1;
  const struct custom_format_request *user_req =
      (const struct custom_format_request *)(uintptr_t)a1;
  if (!readable(user_req, sizeof(*user_req)))
    return -1;
  struct custom_format_request req = *user_req;
  req.device[sizeof(req.device) - 1] = '\0';
  if (req.partition_count == 0 || req.partition_count > 4)
    return -1;
  if (!filesystem_syscall_acquire())
    return -1;
  int32_t result = installer_format_custom_device(
      req.device, req.partition_count, req.sizes_gb);
  filesystem_syscall_unlock();
  return result;
}

long sys_format_device_ex(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (!privileged(PROCESS_CAP_STORAGE_ADMIN))
    return -1;
  const struct format_request *req =
      (const struct format_request *)(uintptr_t)a1;
  if (!readable(req, sizeof(*req)))
    return -1;
  struct format_request r = *req;
  r.device[sizeof(r.device) - 1] = '\0';
  r.serial[sizeof(r.serial) - 1] = '\0';
  r.erase[sizeof(r.erase) - 1] = '\0';
  if (!filesystem_syscall_acquire())
    return -1;
  int32_t res = vfs_format_device_ex(
      r.device, r.serial, r.erase,
      VFS_FS_EXT2);
  filesystem_syscall_unlock();
  return res;
}

long sys_ext2_stat(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  const char *path = (const char *)(uintptr_t)a1;
  struct ext2_stat_info *out = (struct ext2_stat_info *)(uintptr_t)a2;
  if (!readable_string(path) || !writable(out, sizeof(*out)))
    return -1;
  if (!filesystem_syscall_acquire())
    return -1;
  int32_t r = vfs_ext2_stat(path, out);
  filesystem_syscall_unlock();
  return r;
}

long sys_ext2_inode(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  uint32_t ino = (uint32_t)a1;
  struct ext2_stat_info *out = (struct ext2_stat_info *)(uintptr_t)a2;
  if (!writable(out, sizeof(*out)))
    return -1;
  if (!filesystem_syscall_acquire())
    return -1;
  int32_t r = vfs_ext2_inode(ino, out);
  filesystem_syscall_unlock();
  return r;
}

long sys_ext2_super(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  struct ext2_super_info *out = (struct ext2_super_info *)(uintptr_t)a1;
  if (!writable(out, sizeof(*out)))
    return -1;
  if (!filesystem_syscall_acquire())
    return -1;
  int32_t r = vfs_ext2_super(out);
  filesystem_syscall_unlock();
  return r;
}

long sys_ext2_blocks(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  const char *path = (const char *)(uintptr_t)a1;
  struct ext2_blocks_info *out = (struct ext2_blocks_info *)(uintptr_t)a2;
  if (!readable_string(path) || !writable(out, sizeof(*out)))
    return -1;
  if (!filesystem_syscall_acquire())
    return -1;
  int32_t r = vfs_ext2_blocks(path, out);
  filesystem_syscall_unlock();
  return r;
}

long sys_install_device(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (!privileged(PROCESS_CAP_STORAGE_ADMIN))
    return -1;
  if (!readable_string((const char *)(uintptr_t)a1) ||
      !readable_string((const char *)(uintptr_t)a2))
    return -1;
  if (!filesystem_syscall_acquire())
    return -1;
  int32_t result = vfs_install_device((const char *)(uintptr_t)a1,
                                      (const char *)(uintptr_t)a2, 0);
  filesystem_syscall_unlock();
  return result;
}

long sys_install_start(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (!privileged(PROCESS_CAP_STORAGE_ADMIN))
    return -10;
  if (!readable_string((const char *)(uintptr_t)a1) ||
      !readable_string((const char *)(uintptr_t)a2))
    return -11;
  if (install_job.state == 1)
    return -12;
  memset(&install_job, 0, sizeof(install_job));
  memset(&install_history, 0, sizeof(install_history));
  installer_set_accounts(0);
  strncpy(install_device, (const char *)(uintptr_t)a1,
          sizeof(install_device) - 1);
  strncpy(install_serial, (const char *)(uintptr_t)a2,
          sizeof(install_serial) - 1);
  install_job.state = 1;
  install_progress(1, "Starting installer worker");
  if (scheduler_create_thread(install_worker, 0, "installer-io",
                              INSTALL_WORKER_PRIORITY, 0) < 0)
  {
    install_job.state = 3;
    install_job.result = -1;
    install_progress(100, "Cannot start installer worker");
    return -1;
  }
  return 0;
}

long sys_install_start_ex(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (!privileged(PROCESS_CAP_STORAGE_ADMIN))
    return -10;
  const struct install_start_request *req =
      (const struct install_start_request *)(uintptr_t)a1;
  if (!readable(req, sizeof(*req)))
    return -11;
  if (install_job.state == 1)
    return -12;
  if (req->accounts.present && !install_accounts_valid(&req->accounts))
    return -14;
  memset(&install_job, 0, sizeof(install_job));
  memset(&install_history, 0, sizeof(install_history));
  installer_set_accounts(&req->accounts);
  strncpy(install_device, req->device, sizeof(install_device) - 1);
  strncpy(install_serial, req->serial, sizeof(install_serial) - 1);
  install_job.state = 1;
  install_progress(1, "Starting installer worker");
  if (scheduler_create_thread(install_worker, 0, "installer-io",
                              INSTALL_WORKER_PRIORITY, 0) < 0)
  {
    install_job.state = 3;
    install_job.result = -1;
    install_progress(100, "Cannot start installer worker");
    return -1;
  }
  return 0;
}

long sys_install_status(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (!writable((void *)(uintptr_t)a1, sizeof(install_job)))
    return -1;
  *(struct install_status *)(uintptr_t)a1 = install_job;
  return 0;
}

long sys_install_log(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (!writable((void *)(uintptr_t)a1, sizeof(install_history)))
    return -1;
  *(struct install_log *)(uintptr_t)a1 = install_history;
  return 0;
}

long sys_get_root_device(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  char *buffer = (char *)(uintptr_t)a1;
  uint32_t capacity = (uint32_t)a2;
  if (!writable(buffer, capacity))
    return -1;
  const char *root = vfs_root_device_name();
  if (!root || !root[0])
    return -1;
  uint32_t len = (uint32_t)strlen(root);
  if (len + 1 > capacity)
    return -1;
  memcpy(buffer, root, len + 1);
  return (int64_t)len;
}

long sys_get_fs_type(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  char *buffer = (char *)(uintptr_t)a1;
  uint32_t capacity = (uint32_t)a2;
  if (!writable(buffer, capacity))
    return -1;
  const char *name = vfs_fs_type_name(vfs_root_fs_type());
  uint32_t len = (uint32_t)strlen(name);
  if (len + 1 > capacity)
    return -1;
  memcpy(buffer, name, len + 1);
  return (int64_t)len;
}

long sys_save_klog(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  const struct save_klog_request *user_req =
      (const struct save_klog_request *)(uintptr_t)a1;
  if (!readable(user_req, sizeof(*user_req)))
    return -1;
  struct save_klog_request req = *user_req;
  req.device[sizeof(req.device) - 1] = '\0';
  req.path[sizeof(req.path) - 1] = '\0';
  for (uint32_t i = 0; i < sizeof(req.device); i++)
    if (!req.device[i])
      break;
    else if (req.device[i] == '\n' || req.device[i] == '\r')
      req.device[i] = '\0';
  for (uint32_t i = 0; i < sizeof(req.path); i++)
    if (!req.path[i])
      break;
    else if (req.path[i] == '\n' || req.path[i] == '\r')
      req.path[i] = '\0';
  if (!req.device[0] || !req.path[0])
    return -1;
  return vfs_save_klog_to_device(req.device, req.path);
}
