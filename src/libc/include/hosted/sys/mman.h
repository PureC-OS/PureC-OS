#pragma once

#include <stddef.h>

#define PROT_NONE 0
#define PROT_READ 1
#define PROT_WRITE 2
#define PROT_EXEC 4

#define MAP_SHARED 1
#define MAP_PRIVATE 2
#define MAP_FIXED 0x10
#define MAP_ANONYMOUS 0x20
#define MAP_ANON MAP_ANONYMOUS
#define MAP_NORESERVE 0x4000
#define MAP_POPULATE 0x8000
#define MAP_STACK 0x20000
#define MAP_FAILED ((void *)-1)

#define MREMAP_MAYMOVE 1

#define MS_ASYNC 1
#define MS_INVALIDATE 2
#define MS_SYNC 4

#define MADV_NORMAL 0
#define MADV_RANDOM 1
#define MADV_SEQUENTIAL 2
#define MADV_WILLNEED 3
#define MADV_DONTNEED 4
#define MADV_FREE 8
#define MADV_REMOVE 9
#define MADV_DONTFORK 10
#define MADV_DOFORK 11
#define MADV_HUGEPAGE 14
#define MADV_NOHUGEPAGE 15
#define MADV_DONTDUMP 16
#define MADV_DODUMP 17

#define MFD_CLOEXEC 1U
#define MFD_ALLOW_SEALING 2U

#ifdef __cplusplus
extern "C" {
#endif
void *mmap(void *address, size_t length, int prot, int flags, int fd,
           long offset);
int mprotect(void *address, size_t length, int prot);
int munmap(void *address, size_t length);
void *mremap(void *address, size_t old_length, size_t new_length, int flags,
             ...);
int msync(void *address, size_t length, int flags);
int madvise(void *address, size_t length, int advice);
int memfd_create(const char *name, unsigned int flags);
int shm_open(const char *name, int flags, unsigned int mode);
int shm_unlink(const char *name);
int mlock(const void *address, size_t length);
int munlock(const void *address, size_t length);
#ifdef __cplusplus
}
#endif
