#pragma once
// PureC hosted libc: time. time()/gettimeofday()/clock_gettime() return
// real calendar time from the kernel wall clock (RTC, UTC epoch 1970).
// localtime() applies the kernel time zone offset; no DST.

#include <stddef.h>

typedef long time_t;
typedef long clock_t;

#define CLOCKS_PER_SEC 1000

struct tm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
};

#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1

struct timespec {
    time_t tv_sec;
    long tv_nsec;
};

int clock_gettime(int clock_id, struct timespec *out);
time_t time(time_t *result);
clock_t clock(void);
double difftime(time_t end, time_t start);
time_t mktime(struct tm *parts);
struct tm *gmtime(const time_t *value);
struct tm *localtime(const time_t *value);
char *asctime(const struct tm *parts);
char *ctime(const time_t *value);
size_t strftime(char *buffer, size_t capacity, const char *format,
                const struct tm *parts);
