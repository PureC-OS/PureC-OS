#include <lib/string.h>
#include <mm/pmm.h>
#include <mm/shm.h>
#include <kernel/process/process.h>
#include <kernel/process/fdtable.h>
#include <kernel/process/ofd.h>
#include "../common.h"

long sys_mmap(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  const uint64_t legacy_size = offsetof(struct kernel_mmap_args, fd);
  if (!readable((const void *)(uintptr_t)a1, legacy_size))
    return -KERR_FAULT;
  struct kernel_mmap_args request;
  memset(&request, 0, sizeof(request));
  memcpy(&request, (const void *)(uintptr_t)a1, legacy_size);
  if (!(request.flags & KMAP_SHARED))
    return process_mmap(request.address, request.length, request.prot,
                        request.flags);
  if (!readable((const void *)(uintptr_t)a1, sizeof(request)))
    return -KERR_FAULT;
  memcpy(&request, (const void *)(uintptr_t)a1, sizeof(request));
  if (request.offset & (PMM_PAGE_SIZE - 1))
    return -KERR_INVAL;
  int32_t shm;
  if (request.fd < 0)
  {
    if (!request.length)
      return -KERR_INVAL;
    shm = shm_create((request.length + PMM_PAGE_SIZE - 1) &
                     ~(PMM_PAGE_SIZE - 1));
    if (shm < 0)
      return shm;
  }
  else
  {
    int32_t object = fd_object((int32_t)request.fd);
    if (object < 0)
      return -KERR_BADF;
    shm = ofd_shm_id(object);
    if (shm < 0)
      return -KERR_NODEV;
    shm_ref(shm);
  }
  int64_t mapped = process_mmap_shared(request.address, request.length,
                                       request.prot, request.flags, shm,
                                       request.offset / PMM_PAGE_SIZE);
  shm_unref(shm);
  return mapped;
}

long sys_munmap(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  return process_munmap(a1, a2);
}

long sys_mprotect(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a4;
  (void)a5;
  (void)a6;
  return process_mprotect(a1, a2, a3);
}

long sys_mremap(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a5;
  (void)a6;
  return process_mremap(a1, a2, a3, a4);
}

long sys_madvise(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a4;
  (void)a5;
  (void)a6;
  return process_madvise(a1, a2, a3);
}

long sys_heap_grow(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  uint64_t address = process_heap_grow(a1);
  return address ? (int64_t)address : -1;
}

long sys_memfd_create(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (!readable_string((const char *)(uintptr_t)a1))
    return -KERR_FAULT;
  int32_t shm = shm_create(0);
  if (shm < 0)
    return shm;
  return fd_install_shm(shm, (a2 & 1) ? 02000000U : 0U);
}

long sys_shm_open(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (!readable_string((const char *)(uintptr_t)a1))
    return -KERR_FAULT;
  char name[SHM_NAME_MAX + 1];
  uint64_t length = strlen((const char *)(uintptr_t)a1);
  if (length > SHM_NAME_MAX)
    return -KERR_NAMETOOLONG;
  memcpy(name, (const void *)(uintptr_t)a1, length + 1);
  int32_t shm;
  int32_t status = shm_open_named(name, (a2 & 0100) != 0, (a2 & 0200) != 0, &shm);
  if (status < 0)
    return status;
  if (a2 & 01000)
    (void)shm_truncate(shm, 0);
  return fd_install_shm(shm, (uint32_t)a2 & 02000000U);
}

long sys_shm_unlink(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (!readable_string((const char *)(uintptr_t)a1))
    return -KERR_FAULT;
  return shm_unlink_named((const char *)(uintptr_t)a1);
}
