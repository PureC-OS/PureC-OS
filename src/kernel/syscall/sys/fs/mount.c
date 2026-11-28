#include <fs/vfs.h>
#include <fs/vfs_path.h>
#include <kernel/process/process.h>
#include "../common.h"

static bool mount_admin(void)
{
  return process_current_uid() == 0;
}

static int32_t user_path(uint64_t pointer, char *out, uint32_t capacity)
{
  if (!readable_string((const char *)(uintptr_t)pointer))
    return -KERR_FAULT;
  int32_t status = process_resolve_path((const char *)(uintptr_t)pointer, out,
                                        capacity);
  return status < 0 ? -KERR_NAMETOOLONG : 0;
}

long sys_mount(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a6;
  if (!mount_admin())
    return -KERR_PERM;
  char target[VFS_PATH_MAX];
  int32_t status = user_path(a2, target, sizeof(target));
  if (status < 0)
    return status;
  char source[VFS_PATH_MAX];
  const char *source_name = 0;
  if (a1)
  {
    if (!readable_string((const char *)(uintptr_t)a1))
      return -KERR_FAULT;
    uint32_t flags = (uint32_t)a4;
    if (flags & 4096U)
    {
      status = user_path(a1, source, sizeof(source));
      if (status < 0)
        return status;
      source_name = source;
    }
    else
      source_name = (const char *)(uintptr_t)a1;
  }
  const char *fstype = 0;
  if (a3)
  {
    if (!readable_string((const char *)(uintptr_t)a3))
      return -KERR_FAULT;
    fstype = (const char *)(uintptr_t)a3;
  }
  const char *data = 0;
  if (a5)
  {
    if (!readable_string((const char *)(uintptr_t)a5))
      return -KERR_FAULT;
    data = (const char *)(uintptr_t)a5;
  }
  if (!filesystem_syscall_acquire())
    return -KERR_AGAIN;
  long result = vfs_mount_syscall(source_name, target, fstype, (uint32_t)a4,
                                  data);
  filesystem_syscall_unlock();
  return result;
}

long sys_umount2(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (!mount_admin())
    return -KERR_PERM;
  char target[VFS_PATH_MAX];
  int32_t status = user_path(a1, target, sizeof(target));
  if (status < 0)
    return status;
  if (!filesystem_syscall_acquire())
    return -KERR_AGAIN;
  long result = vfs_umount_syscall(target, (uint32_t)a2);
  filesystem_syscall_unlock();
  return result;
}
