#pragma once

#include "../time.h"

#ifdef __cplusplus
extern "C" {
#endif
#define TFD_NONBLOCK 04000
#define TFD_CLOEXEC 02000000
#define TFD_TIMER_ABSTIME 1
#define TFD_TIMER_CANCEL_ON_SET 2

int timerfd_create(int clock_id, int flags);
int timerfd_settime(int fd, int flags, const struct itimerspec *value,
                    struct itimerspec *old_value);
int timerfd_gettime(int fd, struct itimerspec *value);
#ifdef __cplusplus
}
#endif
