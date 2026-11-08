#pragma once
// PureC hosted libc: utime.

#include "time.h"

#ifdef __cplusplus
extern "C" {
#endif
struct utimbuf {
    time_t actime;
    time_t modtime;
};

int utime(const char *path, const struct utimbuf *times);
#ifdef __cplusplus
}
#endif
