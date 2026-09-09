#pragma once
// PureC hosted libc: time. Clock is uptime-based (no RTC in userspace):
// time()/gettimeofday() count seconds since boot (UTC, no DST).

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

time_t time(time_t *result);
clock_t clock(void);
// NOTE: no difftime() — it returns double, and userspace forbids SSE
// (-mgeneral-regs-only, kernel keeps no FPU state). Same reason strtod
// is deferred to the soft-float experiment.
time_t mktime(struct tm *parts);
struct tm *gmtime(const time_t *value);
struct tm *localtime(const time_t *value);
char *asctime(const struct tm *parts);
char *ctime(const time_t *value);
size_t strftime(char *buffer, size_t capacity, const char *format,
                const struct tm *parts);
