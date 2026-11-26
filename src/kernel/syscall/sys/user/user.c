#include <fs/vfs.h>
#include <kernel/syscall/sys/user/sudo.h>
#include <kernel/process/process.h>
#include <kernel/process/scheduler.h>
#include "../common.h"

long sys_getuid(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a1;
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  return (int64_t)process_current_uid();
}

long sys_getgid(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a1;
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  return (int64_t)process_current_gid();
}

long sys_setuid(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  return process_set_uid((uint32_t)a1);
}

long sys_setgid(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  return process_set_gid((uint32_t)a1);
}

long sys_sudo(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (!readable_string((const char *)(uintptr_t)a1))
    return -KERR_FAULT;
  if (!filesystem_syscall_acquire())
    return -1;
  int32_t result = sudo_elevate((const char *)(uintptr_t)a1);
  filesystem_syscall_unlock();
  if (result < 0)
    scheduler_sleep(1500);
  return result;
}

long sys_user_lookup(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a4;
  (void)a5;
  (void)a6;
  if ((a1 && !readable_string((const char *)(uintptr_t)a1)) ||
      !writable((void *)(uintptr_t)a3, sizeof(struct user_info)))
    return -1;
  if (!filesystem_syscall_acquire())
    return -1;
  int32_t result = vfs_user_lookup((const char *)(uintptr_t)a1, (uint32_t)a2,
                                   (struct user_info *)(uintptr_t)a3);
  filesystem_syscall_unlock();
  return result;
}

long sys_session_begin(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  return process_session_begin((uint32_t)a1, (uint32_t)a2);
}

long sys_file_perm(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (!readable_string((const char *)(uintptr_t)a1) ||
      !writable((void *)(uintptr_t)a2, sizeof(struct file_perm_info)))
    return -1;
  if (!filesystem_syscall_acquire())
    return -1;
  int32_t result = vfs_get_perm((const char *)(uintptr_t)a1,
                                (struct file_perm_info *)(uintptr_t)a2);
  filesystem_syscall_unlock();
  return result;
}
