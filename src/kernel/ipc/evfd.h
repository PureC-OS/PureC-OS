#pragma once

#include <stdbool.h>
#include <stdint.h>

#define EVFD_SEMAPHORE 1U
#define EVFD_NONBLOCK 04000U
#define EVFD_CLOEXEC 02000000U

#define TIMERFD_ABSTIME 1U

#define EPOLL_OP_ADD 1
#define EPOLL_OP_DEL 2
#define EPOLL_OP_MOD 3

#define EPOLLIN_BIT 0x001U
#define EPOLLOUT_BIT 0x004U
#define EPOLLERR_BIT 0x008U
#define EPOLLHUP_BIT 0x010U
#define EPOLLRDHUP_BIT 0x2000U
#define EPOLLONESHOT_BIT (1U << 30)
#define EPOLLET_BIT (1U << 31)

struct kernel_timespec {
  int64_t seconds;
  int64_t nanoseconds;
};

struct kernel_itimerspec {
  struct kernel_timespec interval;
  struct kernel_timespec value;
};

struct __attribute__((packed)) kernel_epoll_event {
  uint32_t events;
  uint64_t data;
};

int32_t evfd_eventfd_create(uint64_t initial, uint32_t flags);
int32_t evfd_timerfd_create(int32_t clock_id);
int32_t evfd_epoll_create(void);
void evfd_release(int32_t id);
int32_t evfd_read(int32_t id, void *buffer, uint32_t count, bool nonblock);
int32_t evfd_write(int32_t id, const void *buffer, uint32_t count,
                   bool nonblock);
int32_t evfd_poll(int32_t id);
uint64_t evfd_generation(int32_t id);
int32_t evfd_timerfd_settime(int32_t id, uint32_t flags,
                             const struct kernel_itimerspec *value,
                             struct kernel_itimerspec *old_value);
int32_t evfd_timerfd_gettime(int32_t id, struct kernel_itimerspec *out);
int32_t evfd_epoll_ctl(int32_t id, int32_t op, int32_t fd,
                       const struct kernel_epoll_event *event);
int32_t evfd_epoll_wait(int32_t id, struct kernel_epoll_event *events,
                        int32_t max_events, int64_t timeout_ms);
