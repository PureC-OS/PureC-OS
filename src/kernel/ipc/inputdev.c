#include "inputdev.h"
#include "../../drivers/interrupts/timer.h"
#include "../../lib/string.h"
#include "../process/scheduler.h"
#include "../process/signal.h"
#include "../sync/spinlock.h"
#include "../syscall/syscall.h"

#define INPUT_RING 512

static spinlock_t input_lock = SPINLOCK_INIT;
static struct input_record ring[INPUT_RING];
static uint32_t head;
static uint32_t tail;
static volatile bool opened;
static volatile uint64_t generation;

int32_t inputdev_open(void)
{
  uint64_t flags = spin_lock_irqsave(&input_lock);
  if (opened)
  {
    spin_unlock_irqrestore(&input_lock, flags);
    return -KERR_BUSY;
  }
  opened = true;
  head = tail = 0;
  spin_unlock_irqrestore(&input_lock, flags);
  return 0;
}

void inputdev_release(void)
{
  uint64_t flags = spin_lock_irqsave(&input_lock);
  opened = false;
  head = tail = 0;
  generation++;
  spin_unlock_irqrestore(&input_lock, flags);
}

bool inputdev_active(void) { return opened; }

void inputdev_push(const struct input_record *record)
{
  if (!opened || !record)
    return;
  uint64_t flags = spin_lock_irqsave(&input_lock);
  uint32_t next = (head + 1) % INPUT_RING;
  if (next == tail)
    tail = (tail + 1) % INPUT_RING;
  ring[head] = *record;
  ring[head].time_ms = (uint32_t)timer_ticks();
  head = next;
  generation++;
  spin_unlock_irqrestore(&input_lock, flags);
}

int32_t inputdev_read(void *buffer, uint32_t count, bool nonblock)
{
  uint32_t want = count / (uint32_t)sizeof(struct input_record);
  if (!want)
    return -KERR_INVAL;
  if (want > 32)
    want = 32;
  struct input_record local[32];
  for (;;)
  {
    uint32_t got = 0;
    uint64_t flags = spin_lock_irqsave(&input_lock);
    while (got < want && tail != head)
    {
      local[got++] = ring[tail];
      tail = (tail + 1) % INPUT_RING;
    }
    spin_unlock_irqrestore(&input_lock, flags);
    if (got)
    {
      memcpy(buffer, local, got * sizeof(struct input_record));
      return (int32_t)(got * sizeof(struct input_record));
    }
    if (!opened)
      return 0;
    if (nonblock)
      return -KERR_AGAIN;
    if (signal_pending_current())
      return -KERR_INTR;
    scheduler_sleep(1);
  }
}

int32_t inputdev_poll(void)
{
  uint64_t flags = spin_lock_irqsave(&input_lock);
  bool ready = head != tail;
  spin_unlock_irqrestore(&input_lock, flags);
  return ready ? KPOLLIN : 0;
}

uint64_t inputdev_generation(void) { return generation; }
