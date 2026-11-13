#pragma once

#include <stddef.h>
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif
struct iovec {
    void *iov_base;
    size_t iov_len;
};

#define UIO_MAXIOV 1024

ssize_t readv(int fd, const struct iovec *iov, int count);
ssize_t writev(int fd, const struct iovec *iov, int count);
#ifdef __cplusplus
}
#endif
