#pragma once
// PureC hosted libc: sys/mman. Only declarations: the kernel has no
// executable-memory mapping yet (heap is NX), so mmap/mprotect are
// ENOSYS stubs (see unistd.c). Enough for TCC's AOT path to compile;
// tcc -run (JIT) needs the real syscall, scheduled for phase 3.

#include <stddef.h>

#define PROT_NONE 0
#define PROT_READ 1
#define PROT_WRITE 2
#define PROT_EXEC 4

#define MAP_SHARED 1
#define MAP_PRIVATE 2
#define MAP_FIXED 16
#define MAP_ANONYMOUS 32
#define MAP_ANON MAP_ANONYMOUS
#define MAP_FAILED ((void *)-1)

void *mmap(void *address, size_t length, int prot, int flags, int fd,
           long offset);
int mprotect(void *address, size_t length, int prot);
int munmap(void *address, size_t length);
