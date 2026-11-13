#pragma once

#include <stddef.h>
#include <stdint.h>
#include "time.h"

#define FD_SETSIZE 1024

#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    uint64_t bits[FD_SETSIZE / 64];
} fd_set;

#define FD_ZERO(set) do { for (unsigned _i = 0; _i < FD_SETSIZE / 64; _i++) (set)->bits[_i] = 0; } while (0)
#define FD_SET(fd, set) ((set)->bits[(fd) / 64] |= (1ULL << ((fd) % 64)))
#define FD_CLR(fd, set) ((set)->bits[(fd) / 64] &= ~(1ULL << ((fd) % 64)))
#define FD_ISSET(fd, set) (((set)->bits[(fd) / 64] >> ((fd) % 64)) & 1ULL)

int select(int count, fd_set *read_set, fd_set *write_set, fd_set *error_set,
           struct timeval *timeout);
int pselect(int count, fd_set *read_set, fd_set *write_set, fd_set *error_set,
            const struct timespec *timeout, const void *mask);
#ifdef __cplusplus
}
#endif
