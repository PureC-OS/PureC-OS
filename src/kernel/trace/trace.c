#include "trace.h"
#include <arch/arch.h>
#include "../process/process.h"
#include "../process/scheduler.h"
#include "../sync/spinlock.h"
#include "../../mm/pmm.h"
#include "../../lib/string.h"
#include "../../drivers/interrupts/timer.h"

#define TRACE_MAX_SESSIONS 4
#define TRACE_RING_RECORDS 2048
#define USER_TOP 0x0000800000000000ULL

struct session {
  bool used;
  uint32_t gen;
  uint32_t tracer_pid;
  uint32_t root_pid;
  uint32_t flags;
  uint64_t mask[TRACE_MASK_WORDS];
  struct trace_record *ring;
  uint64_t ring_phys;
  uint64_t ring_pages;
  uint32_t head;
  uint32_t count;
  uint64_t dropped;
  volatile uint32_t resume_seq;
  volatile uint32_t stopped_pid;
  volatile uint32_t stopped_tid;
  volatile uint32_t stopped_nr;
  spinlock_t lock;
};

static struct session sessions[TRACE_MAX_SESSIONS];
static spinlock_t sessions_lock = SPINLOCK_INIT;
static uint32_t next_gen = 1;
static uint64_t tsc_hz;

static uint64_t now_ns(void)
{
  if (!tsc_hz) {
    tsc_hz = arch_cpuid_tsc_hz();
    if (!tsc_hz)
      tsc_hz = 3000000000ULL;
  }
  uint64_t t = arch_read_timestamp();
  return (t / tsc_hz) * 1000000000ULL + (t % tsc_hz) * 1000000000ULL / tsc_hz;
}

static struct session *session_of(struct process *p)
{
  if (!p || !p->trace_slot || p->trace_slot > TRACE_MAX_SESSIONS)
    return 0;
  struct session *s = &sessions[p->trace_slot - 1];
  if (!s->used || s->gen != p->trace_gen) {
    p->trace_slot = 0;
    return 0;
  }
  return s;
}

static void push(struct session *s, const struct trace_record *rec)
{
  uint64_t flags = spin_lock_irqsave(&s->lock);
  if (s->used && s->ring) {
    uint32_t slot;
    if (s->count == TRACE_RING_RECORDS) {
      slot = s->head;
      s->head = (s->head + 1) % TRACE_RING_RECORDS;
      s->dropped++;
    } else {
      slot = (s->head + s->count) % TRACE_RING_RECORDS;
      s->count++;
    }
    s->ring[slot] = *rec;
  }
  spin_unlock_irqrestore(&s->lock, flags);
}

static void fill_header(struct trace_record *rec, uint32_t type, struct process *p, uint64_t nr)
{
  memset(rec, 0, sizeof(*rec));
  rec->type = type;
  rec->pid = p->pid;
  rec->ppid = p->parent_pid;
  struct thread *t = scheduler_current_thread();
  rec->tid = t ? t->id : 0;
  rec->nr = (uint32_t)nr;
  memcpy(rec->comm, p->name, sizeof(rec->comm) - 1);
  rec->time_ns = now_ns();
}

static bool copy_user_string(uint64_t addr, char *out)
{
  if (addr < 4096 || addr >= USER_TOP - TRACE_STR_MAX)
    return false;
  for (uint32_t i = 0; i + 1 < TRACE_STR_MAX; i++) {
    uint64_t a = addr + i;
    if ((i == 0 || (a & 4095) == 0) && !process_user_buffer((const void *)(uintptr_t)a, 1, false))
      return false;
    char c = *(volatile const char *)(uintptr_t)a;
    if (!c) {
      out[i] = 0;
      return i > 0;
    }
    if ((unsigned char)c < 32 || (unsigned char)c > 126)
      return false;
    out[i] = c;
  }
  out[TRACE_STR_MAX - 1] = 0;
  return true;
}

static void free_session_locked(struct session *s)
{
  s->used = false;
  s->resume_seq++;
  if (s->ring)
    pmm_free_contiguous(s->ring_phys, s->ring_pages);
  s->ring = 0;
  s->count = 0;
}

int32_t trace_attach(uint32_t pid, uint32_t flags)
{
  struct process *caller = process_current();
  if (!caller || pid <= 1 || pid >= PROCESS_KTHREAD_PID_BASE || pid == caller->pid)
    return -1;
  uint64_t pages = (sizeof(struct trace_record) * TRACE_RING_RECORDS + 4095) / 4096;
  uint64_t phys = pmm_allocate_contiguous(pages);
  if (!phys)
    return -2;
  process_table_lock();
  struct process *target = process_lookup_by_pid(pid);
  int32_t id = -1;
  if (target && (caller->uid == 0 || caller->uid == target->uid) && !session_of(target)) {
    uint64_t f = spin_lock_irqsave(&sessions_lock);
    for (int32_t i = 0; i < TRACE_MAX_SESSIONS; i++) {
      if (sessions[i].used)
        continue;
      struct session *s = &sessions[i];
      memset(s, 0, sizeof(*s));
      s->used = true;
      s->gen = next_gen++;
      s->tracer_pid = caller->pid;
      s->root_pid = pid;
      s->flags = flags & (TRACE_FOLLOW | TRACE_STOP_MODE);
      s->ring_phys = phys;
      s->ring_pages = pages;
      s->ring = pmm_physical_to_virtual(phys);
      target->trace_slot = (uint8_t)(i + 1);
      target->trace_gen = s->gen;
      id = i;
      break;
    }
    spin_unlock_irqrestore(&sessions_lock, f);
  }
  process_table_unlock();
  if (id < 0)
    pmm_free_contiguous(phys, pages);
  return id;
}

static struct session *owned(int32_t id)
{
  struct process *caller = process_current();
  if (!caller || id < 0 || id >= TRACE_MAX_SESSIONS || !sessions[id].used ||
      sessions[id].tracer_pid != caller->pid)
    return 0;
  return &sessions[id];
}

int32_t trace_read(int32_t id, struct trace_record *out, uint32_t max, uint32_t timeout_ms)
{
  struct session *s = owned(id);
  if (!s || !max)
    return -1;
  if (!process_user_buffer(out, (uint64_t)max * sizeof(*out), true))
    return -1;
  uint64_t start = timer_ticks();
  static struct trace_record batch[32];
  for (;;) {
    uint32_t n = 0;
    uint64_t f = spin_lock_irqsave(&s->lock);
    if (!s->used) {
      spin_unlock_irqrestore(&s->lock, f);
      return -1;
    }
    uint32_t want = max < 32 ? max : 32;
    while (n < want && s->count) {
      batch[n++] = s->ring[s->head];
      s->head = (s->head + 1) % TRACE_RING_RECORDS;
      s->count--;
    }
    spin_unlock_irqrestore(&s->lock, f);
    if (n) {
      memcpy(out, batch, n * sizeof(*out));
      return (int32_t)n;
    }
    if (timer_ticks() - start >= timeout_ms)
      return 0;
    scheduler_sleep(5);
  }
}

int32_t trace_control(int32_t id, uint32_t op, uint64_t arg)
{
  struct session *s = owned(id);
  if (!s)
    return -1;
  switch (op) {
  case TRACE_OP_CONTINUE:
    s->stopped_pid = 0;
    s->stopped_tid = 0;
    s->resume_seq++;
    return 0;
  case TRACE_OP_SET_STOP_MASK: {
    uint64_t mask[TRACE_MASK_WORDS];
    if (!process_user_buffer((const void *)(uintptr_t)arg, sizeof(mask), false))
      return -1;
    memcpy(mask, (const void *)(uintptr_t)arg, sizeof(mask));
    bool any = false;
    for (int i = 0; i < TRACE_MASK_WORDS; i++)
      any |= mask[i] != 0;
    memcpy(s->mask, mask, sizeof(mask));
    if (any)
      s->flags |= TRACE_STOP_MODE;
    else
      s->flags &= ~TRACE_STOP_MODE;
    return 0;
  }
  case TRACE_OP_DETACH: {
    uint64_t f = spin_lock_irqsave(&sessions_lock);
    free_session_locked(s);
    spin_unlock_irqrestore(&sessions_lock, f);
    return 0;
  }
  case TRACE_OP_STATUS: {
    struct trace_status st;
    if (!process_user_buffer((void *)(uintptr_t)arg, sizeof(st), true))
      return -1;
    st.root_pid = s->root_pid;
    st.stopped_pid = s->stopped_pid;
    st.stopped_tid = s->stopped_tid;
    st.stopped_nr = s->stopped_nr;
    st.dropped = s->dropped;
    st.pending = s->count;
    memcpy((void *)(uintptr_t)arg, &st, sizeof(st));
    return 0;
  }
  default:
    return -1;
  }
}

void trace_fork(struct process *parent, struct process *child)
{
  child->trace_slot = 0;
  child->trace_gen = 0;
  struct session *s = session_of(parent);
  if (s && (s->flags & TRACE_FOLLOW)) {
    child->trace_slot = parent->trace_slot;
    child->trace_gen = parent->trace_gen;
  }
}

void trace_process_exit(struct process *process, int32_t status, int32_t signal)
{
  if (!process)
    return;
  struct session *s = session_of(process);
  if (s) {
    struct trace_record rec;
    fill_header(&rec, TRACE_EV_PROC_EXIT, process, 0);
    rec.ret = status;
    rec.args[0] = (uint64_t)(uint32_t)signal;
    push(s, &rec);
    process->trace_slot = 0;
  }
  uint64_t f = spin_lock_irqsave(&sessions_lock);
  for (int i = 0; i < TRACE_MAX_SESSIONS; i++)
    if (sessions[i].used && sessions[i].tracer_pid == process->pid)
      free_session_locked(&sessions[i]);
  spin_unlock_irqrestore(&sessions_lock, f);
}

static bool masked(const struct session *s, uint64_t nr)
{
  return nr < TRACE_MASK_WORDS * 64 && (s->mask[nr / 64] & (1ULL << (nr % 64)));
}

uint64_t trace_syscall_enter(struct process *p, uint64_t nr, const uint64_t args[6])
{
  struct session *s = session_of(p);
  if (!s)
    return 0;
  struct trace_record rec;
  fill_header(&rec, TRACE_EV_ENTER, p, nr);
  memcpy(rec.args, args, sizeof(rec.args));
  if (copy_user_string(args[0], rec.str))
    rec.flags |= TRACE_REC_STR1;
  else if (copy_user_string(args[1], rec.str))
    rec.flags |= TRACE_REC_STR2;
  push(s, &rec);
  if ((s->flags & TRACE_STOP_MODE) && masked(s, nr)) {
    uint32_t seq = s->resume_seq;
    s->stopped_pid = rec.pid;
    s->stopped_tid = rec.tid;
    s->stopped_nr = (uint32_t)nr;
    rec.type = TRACE_EV_STOPPED;
    push(s, &rec);
    while (s->used && s->resume_seq == seq && s->gen == p->trace_gen)
      scheduler_sleep(1);
  }
  return rec.time_ns ? now_ns() : 0;
}

void trace_syscall_exit(struct process *p, uint64_t nr, const uint64_t args[6], int64_t ret, uint64_t start)
{
  struct session *s = session_of(p);
  if (!s)
    return;
  struct trace_record rec;
  fill_header(&rec, TRACE_EV_EXIT, p, nr);
  memcpy(rec.args, args, sizeof(rec.args));
  rec.ret = ret;
  rec.duration_ns = start && rec.time_ns > start ? rec.time_ns - start : 0;
  push(s, &rec);
}
