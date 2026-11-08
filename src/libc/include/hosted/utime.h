#pragma once
// PureC hosted libc: utime.

#include "time.h"

struct utimbuf {
    time_t actime;
    time_t modtime;
};

int utime(const char *path, const struct utimbuf *times);
