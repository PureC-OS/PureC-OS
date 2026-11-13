#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
#define EFD_SEMAPHORE 1
#define EFD_NONBLOCK 04000
#define EFD_CLOEXEC 02000000

typedef uint64_t eventfd_t;

int eventfd(unsigned int initial, int flags);
int eventfd_read(int fd, eventfd_t *value);
int eventfd_write(int fd, eventfd_t value);
#ifdef __cplusplus
}
#endif
