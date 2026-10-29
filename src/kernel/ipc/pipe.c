#include "pipe.h"
#include "../../lib/string.h"
#include "../../mm/pmm.h"
#include "../process/scheduler.h"
#include "../process/signal.h"
#include "../sync/mutex.h"
#include "../syscall/syscall.h"

#define PIPE_MAX 128
#define PIPE_PAGES 4
#define PIPE_CAPACITY (PIPE_PAGES * 4096U)
#define PIPE_WAITERS 8

struct pipe {
  bool used;
  uint8_t *buffer;
  uint64_t phys;
  uint32_t head;
  uint32_t count;
  uint32_t readers;
  uint32_t writers;
  int32_t read_waiters[PIPE_WAITERS];
  int32_t write_waiters[PIPE_WAITERS];
};

static mutex_t pipe_mutex;
static struct pipe pipes[PIPE_MAX];

static void waiters_clear(int32_t *list, int32_t tid) {
  for (uint32_t i = 0; i < PIPE_WAITERS; i++)
    if (list[i] == tid)
      list[i] = 0;
}

static bool waiters_add(int32_t *list, int32_t tid) {
  for (uint32_t i = 0; i < PIPE_WAITERS; i++) {
    if (list[i] == tid)
      return true;
  }
  for (uint32_t i = 0; i < PIPE_WAITERS; i++) {
    if (!list[i]) {
      list[i] = tid;
      return true;
    }
  }
  return false;
}

static uint32_t waiters_take(int32_t *list, int32_t *out) {
  uint32_t n = 0;
  for (uint32_t i = 0; i < PIPE_WAITERS; i++) {
    if (list[i]) {
      out[n++] = list[i];
      list[i] = 0;
    }
  }
  return n;
}

static void wake_all(const int32_t *tids, uint32_t count) {
  for (uint32_t i = 0; i < count; i++)
    scheduler_unblock(tids[i]);
}

static struct pipe *pipe_get(int32_t id) {
  if (id < 0 || id >= PIPE_MAX || !pipes[id].used)
    return 0;
  return &pipes[id];
}

int32_t pipe_create(int32_t *id) {
  mutex_lock(&pipe_mutex);
  for (int32_t i = 0; i < PIPE_MAX; i++) {
    if (pipes[i].used)
      continue;
    uint64_t phys = pmm_allocate_contiguous(PIPE_PAGES);
    if (!phys) {
      mutex_unlock(&pipe_mutex);
      return -KERR_NOMEM;
    }
    memset(&pipes[i], 0, sizeof(pipes[i]));
    pipes[i].used = true;
    pipes[i].phys = phys;
    pipes[i].buffer = (uint8_t *)pmm_physical_to_virtual(phys);
    pipes[i].readers = 1;
    pipes[i].writers = 1;
    *id = i;
    mutex_unlock(&pipe_mutex);
    return 0;
  }
  mutex_unlock(&pipe_mutex);
  return -KERR_NFILE;
}

void pipe_ref(int32_t id, bool write_end) {
  mutex_lock(&pipe_mutex);
  struct pipe *pipe = pipe_get(id);
  if (pipe) {
    if (write_end)
      pipe->writers++;
    else
      pipe->readers++;
  }
  mutex_unlock(&pipe_mutex);
}

void pipe_release(int32_t id, bool write_end) {
  int32_t wake[PIPE_WAITERS * 2];
  uint32_t wake_count = 0;
  mutex_lock(&pipe_mutex);
  struct pipe *pipe = pipe_get(id);
  if (pipe) {
    if (write_end && pipe->writers)
      pipe->writers--;
    else if (!write_end && pipe->readers)
      pipe->readers--;
    if (!pipe->readers && !pipe->writers) {
      pmm_free_contiguous(pipe->phys, PIPE_PAGES);
      memset(pipe, 0, sizeof(*pipe));
    } else {
      wake_count += waiters_take(pipe->read_waiters, wake + wake_count);
      wake_count += waiters_take(pipe->write_waiters, wake + wake_count);
    }
  }
  mutex_unlock(&pipe_mutex);
  wake_all(wake, wake_count);
}

int32_t pipe_read(int32_t id, void *buffer, uint32_t count, bool nonblock) {
  if (!count)
    return 0;
  int32_t self = scheduler_current_tid();
  for (;;) {
    int32_t wake[PIPE_WAITERS];
    uint32_t wake_count = 0;
    mutex_lock(&pipe_mutex);
    struct pipe *pipe = pipe_get(id);
    if (!pipe) {
      mutex_unlock(&pipe_mutex);
      return -KERR_BADF;
    }
    waiters_clear(pipe->read_waiters, self);
    if (pipe->count) {
      uint32_t amount = count < pipe->count ? count : pipe->count;
      uint8_t *out = (uint8_t *)buffer;
      for (uint32_t i = 0; i < amount; i++)
        out[i] = pipe->buffer[(pipe->head + i) % PIPE_CAPACITY];
      pipe->head = (pipe->head + amount) % PIPE_CAPACITY;
      pipe->count -= amount;
      wake_count = waiters_take(pipe->write_waiters, wake);
      mutex_unlock(&pipe_mutex);
      wake_all(wake, wake_count);
      return (int32_t)amount;
    }
    if (!pipe->writers) {
      mutex_unlock(&pipe_mutex);
      return 0;
    }
    if (nonblock) {
      mutex_unlock(&pipe_mutex);
      return -KERR_AGAIN;
    }
    if (signal_pending_current()) {
      mutex_unlock(&pipe_mutex);
      return -KERR_INTR;
    }
    bool registered = waiters_add(pipe->read_waiters, self);
    mutex_unlock(&pipe_mutex);
    if (registered)
      scheduler_block();
    else
      scheduler_sleep(1);
  }
}

int32_t pipe_write(int32_t id, const void *buffer, uint32_t count,
                   bool nonblock) {
  int32_t self = scheduler_current_tid();
  const uint8_t *in = (const uint8_t *)buffer;
  uint32_t written = 0;
  while (written < count) {
    int32_t wake[PIPE_WAITERS];
    uint32_t wake_count = 0;
    mutex_lock(&pipe_mutex);
    struct pipe *pipe = pipe_get(id);
    if (!pipe) {
      mutex_unlock(&pipe_mutex);
      return written ? (int32_t)written : -KERR_BADF;
    }
    waiters_clear(pipe->write_waiters, self);
    if (!pipe->readers) {
      mutex_unlock(&pipe_mutex);
      signal_raise_current(SIGPIPE);
      return written ? (int32_t)written : -KERR_PIPE;
    }
    uint32_t space = PIPE_CAPACITY - pipe->count;
    if (space) {
      uint32_t amount = (count - written) < space ? (count - written) : space;
      for (uint32_t i = 0; i < amount; i++)
        pipe->buffer[(pipe->head + pipe->count + i) % PIPE_CAPACITY] =
            in[written + i];
      pipe->count += amount;
      written += amount;
      wake_count = waiters_take(pipe->read_waiters, wake);
      mutex_unlock(&pipe_mutex);
      wake_all(wake, wake_count);
      continue;
    }
    if (nonblock) {
      mutex_unlock(&pipe_mutex);
      return written ? (int32_t)written : -KERR_AGAIN;
    }
    if (signal_pending_current()) {
      mutex_unlock(&pipe_mutex);
      return written ? (int32_t)written : -KERR_INTR;
    }
    bool registered = waiters_add(pipe->write_waiters, self);
    mutex_unlock(&pipe_mutex);
    if (registered)
      scheduler_block();
    else
      scheduler_sleep(1);
  }
  return (int32_t)written;
}
