#pragma once

#include <stddef.h>
#include <stdint.h>
#include "time.h"

#define FD_SETSIZE 64

typedef struct {
    uint64_t bits[FD_SETSIZE / 64];
} fd_set;

#define FD_ZERO(set) ((set)->bits[0] = 0)
#define FD_SET(fd, set) ((set)->bits[0] |= (1ULL << (fd)))
#define FD_CLR(fd, set) ((set)->bits[0] &= ~(1ULL << (fd)))
#define FD_ISSET(fd, set) (((set)->bits[0] >> (fd)) & 1ULL)

int select(int count, fd_set *read_set, fd_set *write_set, fd_set *error_set,
           struct timeval *timeout);
