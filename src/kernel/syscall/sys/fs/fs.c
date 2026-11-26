#include <arch/arch.h>
#include <kernel/ipc/pty.h>
#include <io/io_sched.h>
#include <kernel/syscall/syscall.h>
#include <kernel/tty/vtcon.h>
#include <drivers/audio/audio.h>
#include <drivers/display/gop.h>
#include <drivers/display/svga.h>
#include <drivers/display/svga3d.h>
#include <drivers/display/vga.h>
#include <drivers/input/keyboard.h>
#include <drivers/interrupts/timer.h>
#include <drivers/mouse/ps2_mouse.h>
#include <drivers/mouse/usb_mouse.h>
#include <drivers/power/power.h>
#include <drivers/serial/serial.h>
#include <io/storage/ahci.h>
#include <io/storage/block_device.h>
#include <io/storage/storage_probe.h>
#include <drivers/usb/ehci.h>
#include <drivers/usb/xhci.h>
#include <fs/install.h>
#include <fs/install_accounts.h>
#include <fs/vfs.h>
#include <fs/vfs_fat32.h>
#include <fs/vfs_path.h>
#include <drivers/display/display_owner.h>
#include <kernel/ipc/inputdev.h>
#include <kernel/syscall/sys/user/sudo.h>
#include <kernel/trace/trace.h>
#include <lib/string.h>
#include <mm/pmm.h>
#include <mm/shm.h>
#include <kernel/random/krandom.h>
#include <kernel/time/wallclock.h>
#include <net/api/ping.h>
#include <net/api/socket.h>
#include <net/api/sockstat.h>
#include <net/config/dhcp.h>
#include <net/core/net_device.h>
#include <net/name/dns.h>
#include <net/network/ipv4.h>
#include <net/transport/tcp.h>
#include <net/wifi/wifi.h>
#include <kernel/core/system_info.h>
#include <kernel/diagnostics/boot_diag.h>
#include <kernel/diagnostics/klog.h>
#include <kernel/diagnostics/panic.h>
#include <kernel/ipc/evfd.h>
#include <kernel/ipc/ipc.h>
#include <kernel/ipc/unix.h>
#include <kernel/process/process.h>
#include <kernel/process/signal.h>
#include <kernel/process/futex.h>
#include <kernel/process/fdtable.h>
#include <kernel/process/ofd.h>
#include <kernel/process/scheduler.h>
#include <kernel/smp/cpu.h>
#include <kernel/tty/tty.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "../common.h"

long sys_fd_read(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a4;
  (void)a5;
  (void)a6;
  if (a3 > 0x7FFFFFFFULL ||
      !writable((void *)(uintptr_t)a2, a3))
    return -KERR_FAULT;
  return fd_read((int32_t)a1, (void *)(uintptr_t)a2, (uint32_t)a3);
}

long sys_fd_write(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a4;
  (void)a5;
  (void)a6;
  if (a3 > 0x7FFFFFFFULL || !readable((const void *)(uintptr_t)a2, a3))
    return -KERR_FAULT;
  return fd_write((int32_t)a1, (const void *)(uintptr_t)a2, (uint32_t)a3);
}

long sys_fd_open(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (!readable_string((const char *)(uintptr_t)a1))
    return -KERR_FAULT;
  char absolute[512];
  int32_t resolved =
      process_resolve_path((const char *)(uintptr_t)a1, absolute,
                           sizeof(absolute));
  if (resolved < 0)
    return resolved;
  return fd_open(absolute, (uint32_t)a2);
}

long sys_fd_ctl(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a4;
  (void)a5;
  (void)a6;
  return fd_ctl((int32_t)a1, (uint32_t)a2, a3);
}

long sys_fd_stat(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (!writable((void *)(uintptr_t)a2, sizeof(struct file_stat_info)))
    return -KERR_FAULT;
  return fd_stat((int32_t)a1, (struct file_stat_info *)(uintptr_t)a2);
}

long sys_fd_path(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a4;
  (void)a5;
  (void)a6;
  if (a3 > 4096 || !writable((void *)(uintptr_t)a2, a3))
    return -KERR_FAULT;
  return fd_path((int32_t)a1, (char *)(uintptr_t)a2, (uint32_t)a3);
}

long sys_fd_seek(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a4;
  (void)a5;
  (void)a6;
  return fd_seek((int32_t)a1, (int64_t)a2, (uint32_t)a3);
}

long sys_fd_truncate(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  return fd_truncate((int32_t)a1, a2);
}

long sys_write(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  const char *s = (const char *)(uintptr_t)a1;
  uint64_t len = a2;
  if (!readable(s, len))
    return -1;
  struct process *writer = process_current();
  if (writer && writer->descriptors[1] != VFS_FD_STDOUT)
  {
    if (len > 0x7FFFFFFFULL)
      return -1;
    int32_t sent = fd_write(1, s, (uint32_t)len);
    return sent < 0 ? -1 : (int64_t)sent;
  }
  for (uint64_t i = 0; i < len; i++)
    serial_putc(s[i]);
  if (vtcon_ready() && scheduler_is_running())
    vtcon_console_write(s, (uint32_t)len);
  return (int64_t)len;
}

long sys_pipe(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (!writable((void *)(uintptr_t)a1, 2 * sizeof(int32_t)))
    return -KERR_FAULT;
  int32_t pair[2];
  int32_t made = fd_pipe(pair, (uint32_t)a2);
  if (made < 0)
    return made;
  ((int32_t *)(uintptr_t)a1)[0] = pair[0];
  ((int32_t *)(uintptr_t)a1)[1] = pair[1];
  return 0;
}

long sys_dup(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  return fd_dup((int32_t)a1, 0, false);
}

long sys_dup2(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  return fd_dup2((int32_t)a1, (int32_t)a2);
}

long sys_chdir(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (!readable_string((const char *)(uintptr_t)a1))
    return -KERR_FAULT;
  return process_chdir((const char *)(uintptr_t)a1);
}

long sys_getcwd(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  uint32_t cwd_capacity =
      a2 > PROCESS_CWD_CAPACITY ? PROCESS_CWD_CAPACITY : (uint32_t)a2;
  if (!writable((void *)(uintptr_t)a1, cwd_capacity))
    return -KERR_FAULT;
  return process_getcwd((char *)(uintptr_t)a1, cwd_capacity);
}

long sys_sync(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a1;
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  return vfs_sync() < 0 ? -KERR_IO : 0;
}

long sys_file_stat_ex(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (!readable_string((const char *)(uintptr_t)a1) ||
      !writable((void *)(uintptr_t)a2, sizeof(struct file_stat_ex)))
    return -1;
  if (!filesystem_syscall_acquire())
    return -1;
  int32_t result = vfs_stat_ex((const char *)(uintptr_t)a1,
                               (struct file_stat_ex *)(uintptr_t)a2);
  filesystem_syscall_unlock();
  return result;
}

long sys_lstat_ex(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (!readable_string((const char *)(uintptr_t)a1) ||
      !writable((void *)(uintptr_t)a2, sizeof(struct file_stat_ex)))
    return -1;
  if (!filesystem_syscall_acquire())
    return -1;
  int32_t result = vfs_lstat_ex((const char *)(uintptr_t)a1,
                                (struct file_stat_ex *)(uintptr_t)a2);
  filesystem_syscall_unlock();
  return result;
}

long sys_readlink(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a4;
  (void)a5;
  (void)a6;
  if (a3 > VFS_PATH_MAX)
    a3 = VFS_PATH_MAX;
  if (!readable_string((const char *)(uintptr_t)a1) || !a3 ||
      !writable((void *)(uintptr_t)a2, (uint32_t)a3))
    return FS_ERROR_INVALID;
  if (!filesystem_syscall_acquire())
    return -1;
  char link[VFS_PATH_MAX];
  int32_t result = vfs_readlink((const char *)(uintptr_t)a1, link,
                                sizeof(link));
  filesystem_syscall_unlock();
  if (result < 0)
    return result;
  uint32_t copy = (uint32_t)result < (uint32_t)a3 ? (uint32_t)result : (uint32_t)a3;
  memcpy((void *)(uintptr_t)a2, link, copy);
  return (int64_t)copy;
}

long sys_symlink(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (!readable_string((const char *)(uintptr_t)a1) ||
      !readable_string((const char *)(uintptr_t)a2))
    return FS_ERROR_INVALID;
  if (!filesystem_syscall_acquire())
    return -1;
  int32_t result = vfs_symlink((const char *)(uintptr_t)a1,
                               (const char *)(uintptr_t)a2);
  filesystem_syscall_unlock();
  return result;
}

long sys_chmod(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (!readable_string((const char *)(uintptr_t)a1))
    return -1;
  if (!filesystem_syscall_acquire())
    return -1;
  int32_t result = vfs_chmod((const char *)(uintptr_t)a1, (uint16_t)a2);
  filesystem_syscall_unlock();
  return result;
}

long sys_chown(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a4;
  (void)a5;
  (void)a6;
  if (!readable_string((const char *)(uintptr_t)a1))
    return -1;
  if (!filesystem_syscall_acquire())
    return -1;
  int32_t result =
      vfs_chown((const char *)(uintptr_t)a1, (uint32_t)a2, (uint32_t)a3);
  filesystem_syscall_unlock();
  return result;
}

long sys_utimes(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a4;
  (void)a5;
  (void)a6;
  if (!readable_string((const char *)(uintptr_t)a1))
    return -1;
  char absolute[512];
  int32_t resolved = process_resolve_path((const char *)(uintptr_t)a1,
                                          absolute, sizeof(absolute));
  if (resolved < 0)
    return resolved;
  if (!filesystem_syscall_acquire())
    return -1;
  int32_t result = vfs_utimes(absolute, (uint32_t)a2, (uint32_t)a3);
  filesystem_syscall_unlock();
  if (result >= 0)
    return 0;
  switch (result)
  {
  case FS_ERROR_NOT_FOUND:
    return -KERR_NOENT;
  case FS_ERROR_DENIED:
    return -KERR_ACCES;
  case FS_ERROR_READ_ONLY:
    return -30;
  case FS_ERROR_UNSUPPORTED:
    return -KERR_NOSYS;
  case FS_ERROR_INVALID:
    return -KERR_INVAL;
  default:
    return -KERR_IO;
  }
}

long sys_rename_path(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (!readable_string((const char *)(uintptr_t)a1) ||
      !readable_string((const char *)(uintptr_t)a2))
    return FS_ERROR_INVALID;
  char old_abs[512];
  char new_abs[512];
  int32_t resolved = process_resolve_path((const char *)(uintptr_t)a1, old_abs,
                                          sizeof(old_abs));
  if (resolved < 0)
    return FS_ERROR_NOT_FOUND;
  resolved = process_resolve_path((const char *)(uintptr_t)a2, new_abs,
                                  sizeof(new_abs));
  if (resolved < 0)
    return FS_ERROR_NOT_FOUND;
  if (!filesystem_syscall_acquire())
    return FS_ERROR_BUSY;
  int32_t result = vfs_rename_path(old_abs, new_abs);
  filesystem_syscall_unlock();
  return result;
}

long sys_file_move(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (!readable_string((const char *)(uintptr_t)a1) ||
      !readable_string((const char *)(uintptr_t)a2))
    return -1;
  if (!filesystem_syscall_acquire())
    return -1;
  int32_t result =
      vfs_move((const char *)(uintptr_t)a1, (const char *)(uintptr_t)a2);
  filesystem_syscall_unlock();
  return result;
}

long sys_file_open(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (!readable_string((const char *)(uintptr_t)a1))
    return -1;
  if (!filesystem_syscall_acquire())
    return -1;
  int32_t result = vfs_open((const char *)(uintptr_t)a1);
  if (result >= 0)
  {
    int32_t descriptor = process_fd_install(result);
    if (descriptor < 0)
      (void)vfs_close(result);
    result = descriptor;
  }
  filesystem_syscall_unlock();
  return result;
}

long sys_file_read(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a4;
  (void)a5;
  (void)a6;
  if (!writable((void *)(uintptr_t)a2, (uint32_t)a3))
    return -1;
  int32_t descriptor = process_fd_resolve((int32_t)a1);
  if (descriptor < 0)
    return -1;
  if (!filesystem_syscall_acquire())
    return -1;
  int32_t result = vfs_read(descriptor, (void *)(uintptr_t)a2, (uint32_t)a3);
  filesystem_syscall_unlock();
  return result;
}

long sys_file_write(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a4;
  (void)a5;
  (void)a6;
  if (!readable_string((const char *)(uintptr_t)a1) ||
      !readable((const void *)(uintptr_t)a2, (uint32_t)a3))
    return -1;
  if (!filesystem_syscall_acquire())
    return -1;
  int32_t result = vfs_write_file((const char *)(uintptr_t)a1,
                                  (const void *)(uintptr_t)a2, (uint32_t)a3);
  filesystem_syscall_unlock();
  return result;
}

long sys_file_append(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a4;
  (void)a5;
  (void)a6;
  if (!readable_string((const char *)(uintptr_t)a1) ||
      !readable((const void *)(uintptr_t)a2, (uint32_t)a3))
    return -1;
  if (!filesystem_syscall_acquire())
    return -1;
  int32_t result = vfs_append_file((const char *)(uintptr_t)a1,
                                   (const void *)(uintptr_t)a2, (uint32_t)a3);
  filesystem_syscall_unlock();
  return result;
}

long sys_file_seek(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a4;
  (void)a5;
  (void)a6;
  int32_t descriptor = process_fd_resolve((int32_t)a1);
  if (descriptor < 0)
    return -1;
  if (!filesystem_syscall_acquire())
    return -1;
  int64_t result = vfs_seek(descriptor, (int64_t)a2, (uint32_t)a3);
  filesystem_syscall_unlock();
  return result;
}

long sys_file_stat(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (!readable_string((const char *)(uintptr_t)a1) ||
      !writable((void *)(uintptr_t)a2, sizeof(struct file_stat_info)))
    return -1;
  if (!filesystem_syscall_acquire())
    return -1;
  int32_t result = vfs_stat((const char *)(uintptr_t)a1,
                            (struct file_stat_info *)(uintptr_t)a2);
  filesystem_syscall_unlock();
  return result;
}

long sys_file_close(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (!filesystem_syscall_acquire())
    return -1;
  int32_t result = process_fd_close((int32_t)a1);
  filesystem_syscall_unlock();
  return result;
}

long sys_file_delete(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (!readable_string((const char *)(uintptr_t)a1))
    return -1;
  if (!filesystem_syscall_acquire())
    return -1;
  int32_t result = vfs_delete((const char *)(uintptr_t)a1);
  filesystem_syscall_unlock();
  return result;
}

long sys_file_rename(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (!readable_string((const char *)(uintptr_t)a1) ||
      !readable_string((const char *)(uintptr_t)a2))
    return -1;
  if (!filesystem_syscall_acquire())
    return -1;
  int32_t result =
      vfs_rename((const char *)(uintptr_t)a1, (const char *)(uintptr_t)a2);
  filesystem_syscall_unlock();
  return result;
}

long sys_file_create(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (!readable_string((const char *)(uintptr_t)a1))
    return -1;
  if (!filesystem_syscall_acquire())
    return -1;
  int32_t result = vfs_create_file((const char *)(uintptr_t)a1);
  filesystem_syscall_unlock();
  return result;
}

long sys_dir_create(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (!readable_string((const char *)(uintptr_t)a1))
    return -1;
  if (!filesystem_syscall_acquire())
    return -1;
  int32_t result = vfs_create_directory((const char *)(uintptr_t)a1);
  filesystem_syscall_unlock();
  return result;
}

long sys_dir_list(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a4;
  (void)a5;
  (void)a6;
  if (!readable_string((const char *)(uintptr_t)a1) ||
      !writable((void *)(uintptr_t)a2,
                (uint64_t)(uint32_t)a3 * sizeof(struct fs_directory_entry)))
    return -1;
  if (!filesystem_syscall_acquire())
    return -1;
  int32_t result =
      vfs_list((const char *)(uintptr_t)a1,
               (struct fs_directory_entry *)(uintptr_t)a2, (uint32_t)a3);
  filesystem_syscall_unlock();
  return result;
}

long sys_dir_list_long(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a4;
  (void)a5;
  (void)a6;
  if (!readable_string((const char *)(uintptr_t)a1) ||
      !writable((void *)(uintptr_t)a2,
                (uint64_t)(uint32_t)a3 *
                    sizeof(struct fs_directory_entry_long)))
    return -1;
  if (!filesystem_syscall_acquire())
    return -1;
  int32_t result = vfs_list_long(
      (const char *)(uintptr_t)a1,
      (struct fs_directory_entry_long *)(uintptr_t)a2, (uint32_t)a3);
  filesystem_syscall_unlock();
  return result;
}
