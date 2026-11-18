#pragma once
// PureC hosted libc: sys/time (kernel wall clock).

#include "../time.h"

#ifdef __cplusplus
extern "C" {
#endif
typedef long suseconds_t;

struct timeval {
    time_t tv_sec;
    suseconds_t tv_usec;
};

struct timezone {
    int tz_minuteswest;
    int tz_dsttime;
};


#define ITIMER_REAL 0
#define ITIMER_VIRTUAL 1
#define ITIMER_PROF 2

struct itimerval {
    struct timeval it_interval;
    struct timeval it_value;
};

int setitimer(int which, const struct itimerval *value, struct itimerval *old);
int getitimer(int which, struct itimerval *value);
int gettimeofday(struct timeval *tv, void *tz);
int utimes(const char *path, const struct timeval times[2]);
int settimeofday(const struct timeval *tv, const struct timezone *tz);
int adjtime(const struct timeval *delta, struct timeval *olddelta);
#ifdef __cplusplus
}
#endif
