#include "futex.h"
#include "../../drivers/interrupts/timer.h"
#include "process.h"
#include "scheduler.h"
#include "signal.h"
#include "../sync/spinlock.h"
#include "../syscall/syscall.h"
#include <stdbool.h>

#define FUTEX_WAKE_BATCH 32

struct futex_waiter
{
  struct futex_waiter *next;
  uint64_t space;
  uint64_t address;
  int32_t tid;
  bool woken;
};

static spinlock_t futex_lock = SPINLOCK_INIT;
static struct futex_waiter *futex_head;

static void futex_unlink(struct futex_waiter *waiter)
{
  struct futex_waiter **link = &futex_head;
  while (*link && *link != waiter)
    link = &(*link)->next;
  if (*link)
    *link = waiter->next;
  waiter->next = NULL;
}

int32_t futex_wait(uint64_t address_space, uint64_t address, uint32_t expected,
                   uint32_t timeout_ms)
{
  if (address & 3)
    return -KERR_INVAL;
  if (!process_user_buffer((const void *)(uintptr_t)address, sizeof(uint32_t),
                           false))
    return -KERR_FAULT;
  if (process_exit_pending())
    return -KERR_INTR;
  volatile uint32_t *word = (volatile uint32_t *)(uintptr_t)address;
  (void)*word;
  struct futex_waiter waiter = {
      .next = NULL,
      .space = address_space,
      .address = address,
      .tid = scheduler_current_tid(),
      .woken = false,
  };
  uint64_t deadline = timeout_ms == FUTEX_NO_TIMEOUT
                          ? 0
                          : timer_ticks() + (uint64_t)timeout_ms;
  uint64_t flags = spin_lock_irqsave(&futex_lock);
  if (*word != expected)
  {
    spin_unlock_irqrestore(&futex_lock, flags);
    return -KERR_AGAIN;
  }
  waiter.next = futex_head;
  futex_head = &waiter;
  spin_unlock_irqrestore(&futex_lock, flags);
  if (timeout_ms == FUTEX_NO_TIMEOUT)
    scheduler_block();
  else
    scheduler_block_timeout(timeout_ms ? timeout_ms : 1);
  flags = spin_lock_irqsave(&futex_lock);
  bool woken = waiter.woken;
  if (!woken)
    futex_unlink(&waiter);
  spin_unlock_irqrestore(&futex_lock, flags);
  if (woken)
    return 0;
  if (signal_pending_current())
    return -KERR_INTR;
  if (deadline && timer_ticks() >= deadline)
    return -KERR_TIMEDOUT;
  return -KERR_INTR;
}

int32_t futex_wake(uint64_t address_space, uint64_t address, int32_t count)
{
  if (count <= 0)
    return 0;
  int32_t total = 0;
  for (;;)
  {
    int32_t tids[FUTEX_WAKE_BATCH];
    int32_t batch = 0;
    uint64_t flags = spin_lock_irqsave(&futex_lock);
    struct futex_waiter **link = &futex_head;
    while (*link && batch < FUTEX_WAKE_BATCH && total + batch < count)
    {
      struct futex_waiter *waiter = *link;
      if (waiter->space == address_space && waiter->address == address)
      {
        *link = waiter->next;
        waiter->next = NULL;
        waiter->woken = true;
        tids[batch++] = waiter->tid;
      }
      else
      {
        link = &waiter->next;
      }
    }
    spin_unlock_irqrestore(&futex_lock, flags);
    for (int32_t i = 0; i < batch; i++)
      scheduler_unblock(tids[i]);
    total += batch;
    if (batch < FUTEX_WAKE_BATCH || total >= count)
      return total;
  }
}
