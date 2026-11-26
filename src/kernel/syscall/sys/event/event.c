#include <drivers/interrupts/timer.h>
#include <lib/string.h>
#include <kernel/diagnostics/klog.h>
#include <kernel/ipc/evfd.h>
#include <kernel/process/signal.h>
#include <kernel/process/fdtable.h>
#include <kernel/process/ofd.h>
#include <kernel/process/scheduler.h>
#include "../common.h"


#define POLL_MAX_DESCRIPTORS 1024
static uint32_t poll_overshoot_reports;

static int64_t poll_descriptors(struct kernel_pollfd *fds, uint64_t count,
                                int64_t timeout_ms)
{
  if (count > POLL_MAX_DESCRIPTORS ||
      (count && !writable(fds, count * sizeof(*fds))))
    return -KERR_FAULT;
  uint64_t started = timer_ticks();
  for (;;)
  {
    int64_t ready = 0;
    for (uint64_t i = 0; i < count; i++)
    {
      int32_t revents = 0;
      if (fds[i].fd >= 0)
      {
        revents = fd_poll(fds[i].fd);
        revents &= (uint16_t)fds[i].events | KPOLLERR | KPOLLHUP | KPOLLNVAL;
      }
      fds[i].revents = (int16_t)revents;
      if (revents)
        ready++;
    }
    if (ready || !timeout_ms)
      return ready;
    if (timeout_ms > 0 && timer_ticks() - started >= (uint64_t)timeout_ms)
    {
      uint64_t over = timer_ticks() - started - (uint64_t)timeout_ms;
      if (over >= 20 && poll_overshoot_reports < 64)
      {
        poll_overshoot_reports++;
        klogf(KLOG_INFO, "poll: timeout %d ms overshot by %u ms",
              (int32_t)timeout_ms, (uint32_t)over);
      }
      return 0;
    }
    if (signal_pending_current())
      return -KERR_INTR;
    scheduler_sleep(1);
  }
}

long sys_poll(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a4;
  (void)a5;
  (void)a6;
  return poll_descriptors((struct kernel_pollfd *)(uintptr_t)a1, a2,
                          (int64_t)(int32_t)a3);
}

long sys_epoll_create(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  int32_t id = evfd_epoll_create();
  return id < 0 ? id : fd_install_event(id, (uint32_t)a1);
}

long sys_epoll_ctl(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a5;
  (void)a6;
  struct kernel_epoll_event event;
  memset(&event, 0, sizeof(event));
  if (a2 != EPOLL_OP_DEL)
  {
    if (!readable((const void *)(uintptr_t)a4, sizeof(event)))
      return -KERR_FAULT;
    memcpy(&event, (const void *)(uintptr_t)a4, sizeof(event));
  }
  int32_t object = fd_object((int32_t)a1);
  if (object < 0)
    return -KERR_BADF;
  if (ofd_kind_of(object) != FD_KIND_EVENT)
    return -KERR_INVAL;
  return evfd_epoll_ctl(ofd_event_id(object), (int32_t)a2, (int32_t)a3,
                        &event);
}

long sys_epoll_wait(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a5;
  (void)a6;
  int32_t max = (int32_t)a3;
  struct kernel_epoll_event ready[64];
  if (max <= 0)
    return -KERR_INVAL;
  uint32_t bounded = max > 64 ? 64U : (uint32_t)max;
  if (!writable((void *)(uintptr_t)a2, bounded * sizeof(ready[0])))
    return -KERR_FAULT;
  int32_t object = fd_object((int32_t)a1);
  if (object < 0)
    return -KERR_BADF;
  if (ofd_kind_of(object) != FD_KIND_EVENT)
    return -KERR_INVAL;
  int32_t count = evfd_epoll_wait(ofd_event_id(object), ready, (int32_t)bounded,
                                  (int64_t)(int32_t)a4);
  if (count > 0)
    memcpy((void *)(uintptr_t)a2, ready, (uint32_t)count * sizeof(ready[0]));
  return count;
}

long sys_eventfd(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  int32_t id = evfd_eventfd_create(a1, (uint32_t)a2);
  return id < 0 ? id : fd_install_event(id, (uint32_t)a2);
}

long sys_timerfd_create(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  int32_t id = evfd_timerfd_create((int32_t)a1);
  return id < 0 ? id : fd_install_event(id, (uint32_t)a2);
}

long sys_timerfd_settime(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a5;
  (void)a6;
  struct kernel_itimerspec value;
  struct kernel_itimerspec old;
  struct kernel_itimerspec *old_user = (struct kernel_itimerspec *)(uintptr_t)a4;
  if (!readable((const void *)(uintptr_t)a3, sizeof(value)) ||
      (old_user && !writable(old_user, sizeof(old))))
    return -KERR_FAULT;
  memcpy(&value, (const void *)(uintptr_t)a3, sizeof(value));
  int32_t object = fd_object((int32_t)a1);
  if (object < 0 || ofd_kind_of(object) != FD_KIND_EVENT)
    return -KERR_BADF;
  int32_t result = evfd_timerfd_settime(ofd_event_id(object), (uint32_t)a2,
                                        &value, old_user ? &old : 0);
  if (result == 0 && old_user)
    memcpy(old_user, &old, sizeof(old));
  return result;
}

long sys_timerfd_gettime(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  struct kernel_itimerspec value;
  if (!writable((void *)(uintptr_t)a2, sizeof(value)))
    return -KERR_FAULT;
  int32_t object = fd_object((int32_t)a1);
  if (object < 0 || ofd_kind_of(object) != FD_KIND_EVENT)
    return -KERR_BADF;
  int32_t result = evfd_timerfd_gettime(ofd_event_id(object), &value);
  if (result == 0)
    memcpy((void *)(uintptr_t)a2, &value, sizeof(value));
  return result;
}
