#include "evfd.h"
#include "../../drivers/interrupts/timer.h"
#include "../../lib/string.h"
#include "../process/fdtable.h"
#include "../process/ofd.h"
#include "../process/scheduler.h"
#include "../process/signal.h"
#include "../sync/mutex.h"
#include "../syscall/syscall.h"

#define EVOBJ_MAX 512
#define EPOLL_WATCH_MAX 64
#define EPOLL_DEPTH_MAX 2
#define EPOLL_MAX 32
#define EVFD_COUNTER_MAX 0xFFFFFFFFFFFFFFFEULL

enum evobj_type
{
  EVOBJ_FREE = 0,
  EVOBJ_EVENT,
  EVOBJ_TIMER,
  EVOBJ_EPOLL
};

struct epoll_watch
{
  bool used;
  bool disabled;
  int32_t fd;
  int32_t ofd;
  uint32_t events;
  uint32_t last;
  uint64_t data;
};

struct evobj
{
  uint8_t type;
  bool semaphore;
  bool armed;
  uint64_t counter;
  uint64_t interval_ms;
  uint64_t expire_ms;
  uint64_t pending;
  int32_t slot;
};

static mutex_t ev_mutex;
static struct evobj objects[EVOBJ_MAX];
static struct epoll_watch epoll_watches[EPOLL_MAX][EPOLL_WATCH_MAX];
static bool epoll_slot_used[EPOLL_MAX];
static uint32_t epoll_depth;

static struct evobj *object_get(int32_t id, uint8_t type)
{
  if (id < 0 || id >= EVOBJ_MAX || objects[id].type != type)
    return 0;
  return &objects[id];
}

static int32_t object_allocate(uint8_t type)
{
  for (int32_t i = 0; i < EVOBJ_MAX; i++)
  {
    if (objects[i].type != EVOBJ_FREE)
      continue;
    memset(&objects[i], 0, sizeof(objects[i]));
    objects[i].type = type;
    return i;
  }
  return -KERR_NFILE;
}

static uint64_t timespec_to_ms(const struct kernel_timespec *spec)
{
  if (spec->seconds < 0 || spec->nanoseconds < 0)
    return 0;
  uint64_t ms = (uint64_t)spec->seconds * 1000ULL;
  ms += ((uint64_t)spec->nanoseconds + 999999ULL) / 1000000ULL;
  return ms;
}

static void ms_to_timespec(uint64_t ms, struct kernel_timespec *spec)
{
  spec->seconds = (int64_t)(ms / 1000ULL);
  spec->nanoseconds = (int64_t)(ms % 1000ULL) * 1000000LL;
}

int32_t evfd_eventfd_create(uint64_t initial, uint32_t flags)
{
  if (initial > EVFD_COUNTER_MAX)
    return -KERR_INVAL;
  mutex_lock(&ev_mutex);
  int32_t id = object_allocate(EVOBJ_EVENT);
  if (id >= 0)
  {
    objects[id].counter = initial;
    objects[id].semaphore = (flags & EVFD_SEMAPHORE) != 0;
  }
  mutex_unlock(&ev_mutex);
  return id;
}

int32_t evfd_timerfd_create(int32_t clock_id)
{
  if (clock_id != 0 && clock_id != 1 && clock_id != 7)
    return -KERR_INVAL;
  mutex_lock(&ev_mutex);
  int32_t id = object_allocate(EVOBJ_TIMER);
  mutex_unlock(&ev_mutex);
  return id;
}

int32_t evfd_epoll_create(void)
{
  mutex_lock(&ev_mutex);
  int32_t slot = -1;
  for (int32_t i = 0; i < EPOLL_MAX; i++)
  {
    if (!epoll_slot_used[i])
    {
      slot = i;
      break;
    }
  }
  if (slot < 0)
  {
    mutex_unlock(&ev_mutex);
    return -KERR_NFILE;
  }
  int32_t id = object_allocate(EVOBJ_EPOLL);
  if (id >= 0)
  {
    epoll_slot_used[slot] = true;
    memset(epoll_watches[slot], 0, sizeof(epoll_watches[slot]));
    objects[id].slot = slot;
  }
  mutex_unlock(&ev_mutex);
  return id;
}

void evfd_release(int32_t id)
{
  mutex_lock(&ev_mutex);
  if (id >= 0 && id < EVOBJ_MAX && objects[id].type != EVOBJ_FREE)
  {
    if (objects[id].type == EVOBJ_EPOLL)
      epoll_slot_used[objects[id].slot] = false;
    memset(&objects[id], 0, sizeof(objects[id]));
  }
  mutex_unlock(&ev_mutex);
}

static void timer_update(struct evobj *timer)
{
  if (!timer->armed)
    return;
  uint64_t now = timer_ticks();
  if (now < timer->expire_ms)
    return;
  if (timer->interval_ms)
  {
    uint64_t count = 1 + (now - timer->expire_ms) / timer->interval_ms;
    timer->pending += count;
    timer->expire_ms += count * timer->interval_ms;
  }
  else
  {
    timer->pending++;
    timer->armed = false;
  }
}

static bool wait_interrupted(int64_t deadline_ms, bool *timed_out)
{
  if (signal_pending_current())
    return true;
  if (deadline_ms >= 0 && (int64_t)timer_ticks() >= deadline_ms)
  {
    *timed_out = true;
    return false;
  }
  scheduler_sleep(1);
  return false;
}

int32_t evfd_read(int32_t id, void *buffer, uint32_t count, bool nonblock)
{
  if (count < sizeof(uint64_t))
    return -KERR_INVAL;
  for (;;)
  {
    mutex_lock(&ev_mutex);
    struct evobj *object = id >= 0 && id < EVOBJ_MAX ? &objects[id] : 0;
    if (!object || (object->type != EVOBJ_EVENT && object->type != EVOBJ_TIMER))
    {
      mutex_unlock(&ev_mutex);
      return -KERR_BADF;
    }
    uint64_t value = 0;
    if (object->type == EVOBJ_EVENT)
    {
      if (object->counter)
      {
        if (object->semaphore)
        {
          value = 1;
          object->counter--;
        }
        else
        {
          value = object->counter;
          object->counter = 0;
        }
      }
    }
    else
    {
      timer_update(object);
      value = object->pending;
      object->pending = 0;
    }
    mutex_unlock(&ev_mutex);
    if (value)
    {
      memcpy(buffer, &value, sizeof(value));
      return (int32_t)sizeof(value);
    }
    if (nonblock)
      return -KERR_AGAIN;
    bool timed_out = false;
    if (wait_interrupted(-1, &timed_out))
      return -KERR_INTR;
  }
}

int32_t evfd_write(int32_t id, const void *buffer, uint32_t count,
                   bool nonblock)
{
  if (count < sizeof(uint64_t))
    return -KERR_INVAL;
  uint64_t value;
  memcpy(&value, buffer, sizeof(value));
  if (value == UINT64_MAX)
    return -KERR_INVAL;
  for (;;)
  {
    mutex_lock(&ev_mutex);
    struct evobj *object = object_get(id, EVOBJ_EVENT);
    if (!object)
    {
      mutex_unlock(&ev_mutex);
      return -KERR_BADF;
    }
    if (object->counter <= EVFD_COUNTER_MAX - value)
    {
      object->counter += value;
      mutex_unlock(&ev_mutex);
      return (int32_t)sizeof(value);
    }
    mutex_unlock(&ev_mutex);
    if (nonblock)
      return -KERR_AGAIN;
    bool timed_out = false;
    if (wait_interrupted(-1, &timed_out))
      return -KERR_INTR;
  }
}

static int32_t epoll_scan(int32_t id, struct kernel_epoll_event *out,
                          int32_t max_events, bool commit);

int32_t evfd_poll(int32_t id)
{
  mutex_lock(&ev_mutex);
  if (id < 0 || id >= EVOBJ_MAX || objects[id].type == EVOBJ_FREE)
  {
    mutex_unlock(&ev_mutex);
    return KPOLLNVAL;
  }
  struct evobj *object = &objects[id];
  int32_t revents = 0;
  if (object->type == EVOBJ_EVENT)
  {
    if (object->counter)
      revents |= KPOLLIN;
    if (object->counter < EVFD_COUNTER_MAX)
      revents |= KPOLLOUT;
  }
  else if (object->type == EVOBJ_TIMER)
  {
    timer_update(object);
    if (object->pending)
      revents |= KPOLLIN;
  }
  bool is_epoll = object->type == EVOBJ_EPOLL;
  mutex_unlock(&ev_mutex);
  if (is_epoll)
  {
    struct kernel_epoll_event probe;
    if (epoll_scan(id, &probe, 1, false) > 0)
      revents |= KPOLLIN;
  }
  return revents;
}

int32_t evfd_timerfd_settime(int32_t id, uint32_t flags,
                             const struct kernel_itimerspec *value,
                             struct kernel_itimerspec *old_value)
{
  if (flags & ~TIMERFD_ABSTIME)
    return -KERR_INVAL;
  if (value->value.nanoseconds < 0 || value->value.nanoseconds >= 1000000000LL ||
      value->interval.nanoseconds < 0 ||
      value->interval.nanoseconds >= 1000000000LL ||
      value->value.seconds < 0 || value->interval.seconds < 0)
    return -KERR_INVAL;
  mutex_lock(&ev_mutex);
  struct evobj *timer = object_get(id, EVOBJ_TIMER);
  if (!timer)
  {
    mutex_unlock(&ev_mutex);
    return -KERR_BADF;
  }
  timer_update(timer);
  uint64_t now = timer_ticks();
  if (old_value)
  {
    memset(old_value, 0, sizeof(*old_value));
    if (timer->armed)
      ms_to_timespec(timer->expire_ms > now ? timer->expire_ms - now : 0,
                     &old_value->value);
    ms_to_timespec(timer->interval_ms, &old_value->interval);
  }
  uint64_t first = timespec_to_ms(&value->value);
  timer->pending = 0;
  timer->interval_ms = timespec_to_ms(&value->interval);
  if (!first)
  {
    timer->armed = false;
    timer->interval_ms = 0;
  }
  else
  {
    timer->armed = true;
    if (flags & TIMERFD_ABSTIME)
      timer->expire_ms = first;
    else
      timer->expire_ms = now + (first ? first : 1);
  }
  mutex_unlock(&ev_mutex);
  return 0;
}

int32_t evfd_timerfd_gettime(int32_t id, struct kernel_itimerspec *out)
{
  mutex_lock(&ev_mutex);
  struct evobj *timer = object_get(id, EVOBJ_TIMER);
  if (!timer)
  {
    mutex_unlock(&ev_mutex);
    return -KERR_BADF;
  }
  timer_update(timer);
  uint64_t now = timer_ticks();
  memset(out, 0, sizeof(*out));
  if (timer->armed)
    ms_to_timespec(timer->expire_ms > now ? timer->expire_ms - now : 0,
                   &out->value);
  ms_to_timespec(timer->interval_ms, &out->interval);
  mutex_unlock(&ev_mutex);
  return 0;
}

int32_t evfd_epoll_ctl(int32_t id, int32_t op, int32_t fd,
                       const struct kernel_epoll_event *event)
{
  int32_t target = fd_object(fd);
  if (target < 0)
    return -KERR_BADF;
  mutex_lock(&ev_mutex);
  struct evobj *epoll = object_get(id, EVOBJ_EPOLL);
  if (!epoll)
  {
    mutex_unlock(&ev_mutex);
    return -KERR_INVAL;
  }
  if (ofd_kind_of(target) == FD_KIND_EVENT && ofd_event_id(target) == id)
  {
    mutex_unlock(&ev_mutex);
    return -KERR_INVAL;
  }
  struct epoll_watch *found = 0;
  struct epoll_watch *spare = 0;
  for (int32_t i = 0; i < EPOLL_WATCH_MAX; i++)
  {
    struct epoll_watch *watch = &epoll_watches[epoll->slot][i];
    if (!watch->used)
    {
      if (!spare)
        spare = watch;
    }
    else if (watch->fd == fd && watch->ofd == target)
      found = watch;
  }
  int32_t result = 0;
  switch (op)
  {
  case EPOLL_OP_ADD:
    if (found)
      result = -KERR_EXIST;
    else if (!spare)
      result = -KERR_NOMEM;
    else
    {
      spare->used = true;
      spare->disabled = false;
      spare->fd = fd;
      spare->ofd = target;
      spare->events = event->events;
      spare->data = event->data;
      spare->last = 0;
    }
    break;
  case EPOLL_OP_MOD:
    if (!found)
      result = -KERR_NOENT;
    else
    {
      found->events = event->events;
      found->data = event->data;
      found->disabled = false;
      found->last = 0;
    }
    break;
  case EPOLL_OP_DEL:
    if (!found)
      result = -KERR_NOENT;
    else
      found->used = false;
    break;
  default:
    result = -KERR_INVAL;
  }
  mutex_unlock(&ev_mutex);
  return result;
}

static int32_t epoll_scan(int32_t id, struct kernel_epoll_event *out,
                          int32_t max_events, bool commit)
{
  if (__atomic_add_fetch(&epoll_depth, 1, __ATOMIC_SEQ_CST) > EPOLL_DEPTH_MAX)
  {
    __atomic_sub_fetch(&epoll_depth, 1, __ATOMIC_SEQ_CST);
    return 0;
  }
  struct epoll_watch snapshot[EPOLL_WATCH_MAX];
  mutex_lock(&ev_mutex);
  struct evobj *epoll = object_get(id, EVOBJ_EPOLL);
  if (!epoll)
  {
    mutex_unlock(&ev_mutex);
    __atomic_sub_fetch(&epoll_depth, 1, __ATOMIC_SEQ_CST);
    return -KERR_BADF;
  }
  memcpy(snapshot, epoll_watches[epoll->slot], sizeof(snapshot));
  mutex_unlock(&ev_mutex);

  int32_t reported = 0;
  uint32_t ready_now[EPOLL_WATCH_MAX];
  bool dead[EPOLL_WATCH_MAX];
  bool fired[EPOLL_WATCH_MAX];
  for (int32_t i = 0; i < EPOLL_WATCH_MAX; i++)
  {
    ready_now[i] = 0;
    dead[i] = false;
    fired[i] = false;
    struct epoll_watch *watch = &snapshot[i];
    if (!watch->used)
      continue;
    if (fd_object(watch->fd) != watch->ofd)
    {
      dead[i] = true;
      continue;
    }
    int32_t polled = ofd_poll(watch->ofd);
    if (polled & KPOLLNVAL)
    {
      dead[i] = true;
      continue;
    }
    uint32_t revents = (uint32_t)polled & 0xFFFFU;
    if ((revents & KPOLLHUP) && (watch->events & EPOLLRDHUP_BIT))
      revents |= EPOLLRDHUP_BIT;
    uint32_t mask = (watch->events & ~(EPOLLET_BIT | EPOLLONESHOT_BIT)) |
                    EPOLLERR_BIT | EPOLLHUP_BIT;
    uint32_t ready = revents & mask;
    ready_now[i] = ready;
    if (watch->disabled || !ready || reported >= max_events)
      continue;
    uint32_t report = ready;
    if (watch->events & EPOLLET_BIT)
      report = ready & ~watch->last;
    if (!report)
      continue;
    fired[i] = true;
    out[reported].events = report;
    out[reported].data = watch->data;
    reported++;
  }
  if (commit)
  {
    mutex_lock(&ev_mutex);
    epoll = object_get(id, EVOBJ_EPOLL);
    if (epoll)
    {
      for (int32_t i = 0; i < EPOLL_WATCH_MAX; i++)
      {
        struct epoll_watch *live = &epoll_watches[epoll->slot][i];
        struct epoll_watch *seen = &snapshot[i];
        if (!seen->used || !live->used || live->fd != seen->fd ||
            live->ofd != seen->ofd)
          continue;
        if (dead[i])
        {
          live->used = false;
          continue;
        }
        live->last = ready_now[i];
        if (fired[i] && (live->events & EPOLLONESHOT_BIT))
          live->disabled = true;
      }
    }
    mutex_unlock(&ev_mutex);
  }
  __atomic_sub_fetch(&epoll_depth, 1, __ATOMIC_SEQ_CST);
  return reported;
}

int32_t evfd_epoll_wait(int32_t id, struct kernel_epoll_event *events,
                        int32_t max_events, int64_t timeout_ms)
{
  if (max_events <= 0)
    return -KERR_INVAL;
  if (max_events > EPOLL_WATCH_MAX)
    max_events = EPOLL_WATCH_MAX;
  int64_t deadline = timeout_ms < 0 ? -1 : (int64_t)timer_ticks() + timeout_ms;
  for (;;)
  {
    int32_t count = epoll_scan(id, events, max_events, true);
    if (count != 0)
      return count;
    if (timeout_ms == 0)
      return 0;
    bool timed_out = false;
    if (wait_interrupted(deadline, &timed_out))
      return -KERR_INTR;
    if (timed_out)
      return 0;
  }
}
