#include "scheduler.h"
#include "../../arch/x86_64/fpu/include/fpu.h"
#include "../../arch/x86_64/gdt/include/gdt.h"
#include "../../arch/x86_64/idt/include/idt.h"
#include "../../drivers/interrupts/timer.h"
#include "../../lib/string.h"
#include "../../mm/oom/oom_slab.h"
#include "../../mm/pmm.h"
#include "../../mm/vmm.h"
#include "../diagnostics/klog.h"
#include "../diagnostics/ksym.h"
#include "../diagnostics/panic.h"
#include "../smp/cpu.h"
#include "../smp/lapic.h"
#include "../smp/percpu.h"
#include "../smp/smp.h"
#include "../sync/spinlock.h"

_Static_assert(sizeof(struct thread) <= 4096,
               "thread node must fit in a single PMM page");

static struct thread *thread_list;
static uint32_t thread_live_count;
static struct thread idle_threads[CPU_MAX_COUNT];
struct scheduler_cpu
{
  struct thread *current;
  struct thread *idle;
  struct thread *previous;
  bool initialized;
  bool started;
  uint64_t last_tick;
  uint64_t total_ticks;
  uint64_t idle_ticks;
  uint32_t preempt_depth;
  bool reschedule_pending;
  struct thread *rq_head;
  struct thread *rq_tail;
  uint32_t rq_count;
  uint64_t rq_weight;
  uint64_t rq_min_vruntime;
  uint64_t next_balance;
  uint32_t pending_kick;
} __attribute__((aligned(64)));
static struct scheduler_cpu cpu_schedulers[CPU_MAX_COUNT];
static spinlock_t runqueue_lock = SPINLOCK_INIT;
static uint32_t next_id = 1;
static uint32_t active_mask;
static struct thread *sleep_head;
#define SCHEDULER_NICE_0_WEIGHT 1024ULL
#define THASH_BITS 8
#define THASH_SIZE (1u << THASH_BITS)
static struct thread *thash_buckets[THASH_SIZE];
static uint32_t thash_idx(uint32_t tid)
{
  tid ^= tid >> THASH_BITS;
  return tid & (THASH_SIZE - 1);
}
static void thash_insert(struct thread *t)
{
  uint32_t i = thash_idx(t->id);
  t->hnext = thash_buckets[i];
  thash_buckets[i] = t;
}
static void thash_remove(struct thread *t)
{
  uint32_t i = thash_idx(t->id);
  struct thread **link = &thash_buckets[i];
  while (*link && *link != t)
    link = &(*link)->hnext;
  if (*link)
    *link = t->hnext;
  t->hnext = NULL;
}
static struct thread *thash_find(uint32_t tid)
{
  if (!tid)
    return NULL;
  uint32_t i = thash_idx(tid);
  for (struct thread *t = thash_buckets[i]; t; t = t->hnext)
    if (t->id == tid && t->state != THREAD_FREE)
      return t;
  return NULL;
}

static inline void sched_publish_current(uint32_t id, struct thread *thread)
{
#ifndef PUREC_HOST_TEST
  percpu_set_current(id, thread);
#else
  (void)id;
  (void)thread;
#endif
}

static struct scheduler_cpu *local_scheduler(void)
{
  uint32_t id = gdt_current_cpu_id();
  if (id >= CPU_MAX_COUNT)
    kernel_panic("sched: CPU has no kernel GDT");
  return &cpu_schedulers[id];
}

static uint32_t scheduler_weight(uint8_t qos)
{
  static const uint16_t weights[] = {256, 512, 768, 1024, 1536, 2048};
  return qos < sizeof(weights) / sizeof(weights[0])
             ? weights[qos]
             : weights[SCHED_QOS_DEFAULT];
}

uint32_t scheduler_qos_quantum(uint8_t qos)
{
  uint64_t slice = (uint64_t)SCHEDULER_TARGET_LATENCY_MS *
                   scheduler_weight(qos) / SCHEDULER_NICE_0_WEIGHT;
  return slice ? (uint32_t)slice : 1;
}

#define SCHEDULER_BALANCE_INTERVAL_MS 4
#define SCHEDULER_CACHE_HOT_MS 2

static uint64_t saturating_add(uint64_t a, uint64_t b)
{
  return UINT64_MAX - a < b ? UINT64_MAX : a + b;
}

static uint64_t thread_weight(const struct thread *thread)
{
  return scheduler_weight(thread->qos);
}

static bool eligible(const struct thread *t, uint32_t id)
{
  if (t->kernel_only)
    return id == 0;
  return t->affinity < 0 || t->affinity == (int16_t)id;
}

static void unlock_and_kick(struct scheduler_cpu *cpu)
{
  uint32_t mask = cpu->pending_kick;
  cpu->pending_kick = 0;
  spin_unlock(&runqueue_lock);
  for (uint32_t id = 0; mask; id++, mask >>= 1)
  {
    if (mask & 1U)
      smp_reschedule_cpu(id);
  }
}

static void rq_enqueue(uint32_t id, struct thread *thread)
{
  struct scheduler_cpu *cpu = &cpu_schedulers[id];
  thread->rq_next = NULL;
  thread->rq_prev = cpu->rq_tail;
  if (cpu->rq_tail)
    cpu->rq_tail->rq_next = thread;
  else
    cpu->rq_head = thread;
  cpu->rq_tail = thread;
  cpu->rq_count++;
  cpu->rq_weight += thread_weight(thread);
  thread->rq_cpu = (int16_t)id;
}

static void rq_dequeue(struct thread *thread)
{
  if (thread->rq_cpu < 0)
    return;
  struct scheduler_cpu *cpu = &cpu_schedulers[thread->rq_cpu];
  if (thread->rq_prev)
    thread->rq_prev->rq_next = thread->rq_next;
  else
    cpu->rq_head = thread->rq_next;
  if (thread->rq_next)
    thread->rq_next->rq_prev = thread->rq_prev;
  else
    cpu->rq_tail = thread->rq_prev;
  cpu->rq_count--;
  cpu->rq_weight -= thread_weight(thread);
  thread->rq_next = NULL;
  thread->rq_prev = NULL;
  thread->rq_cpu = -1;
}

static uint64_t cpu_load(uint32_t id)
{
  const struct scheduler_cpu *cpu = &cpu_schedulers[id];
  uint64_t load = cpu->rq_weight;
  const struct thread *current = cpu->current;
  if (current && !current->idle)
    load += thread_weight(current);
  return load;
}

static uint64_t vruntime_relative(const struct thread *thread,
                                  const struct scheduler_cpu *cpu)
{
  return thread->virtual_runtime > cpu->rq_min_vruntime
             ? thread->virtual_runtime - cpu->rq_min_vruntime
             : 0;
}

static uint32_t select_cpu(const struct thread *thread, uint32_t here)
{
  uint32_t best = CPU_MAX_COUNT;
  uint64_t best_load = UINT64_MAX;
  uint32_t count = cpu_registered_count();
  if (count > CPU_MAX_COUNT)
    count = CPU_MAX_COUNT;
  for (uint32_t id = 0; id < count; id++)
  {
    if (!cpu_schedulers[id].started || !eligible(thread, id))
      continue;
    uint64_t load = cpu_load(id);
    if (load < best_load)
    {
      best = id;
      best_load = load;
    }
  }
  if (best == CPU_MAX_COUNT)
  {
    if (eligible(thread, here))
      return here;
    if (thread->kernel_only)
      return 0;
    return thread->affinity >= 0 ? (uint32_t)thread->affinity : here;
  }
  int16_t last = thread->last_cpu;
  if (last >= 0 && (uint32_t)last < count && (uint32_t)last != best &&
      cpu_schedulers[last].started && eligible(thread, (uint32_t)last) &&
      cpu_load((uint32_t)last) < best_load + thread_weight(thread))
    return (uint32_t)last;
  return best;
}

static struct thread *running_candidate(const struct scheduler_cpu *cpu)
{
  struct thread *current = cpu->current;
  if (!current || current->idle || current->rq_cpu >= 0)
    return NULL;
  if (current->state != THREAD_READY && current->state != THREAD_RUNNING)
    return NULL;
  return current;
}

static void make_runnable(struct thread *thread, uint32_t here,
                          struct scheduler_cpu *local);
static void thread_block_locked(struct thread *thread,
                                struct scheduler_cpu *cpu, uint64_t wake_tick);
static void requeue_prev(struct thread *prev, uint32_t id,
                         struct scheduler_cpu *cpu);

static uint32_t fair_slice_ms(const struct scheduler_cpu *cpu,
                              const struct thread *thread,
                              const struct thread *running)
{
  uint64_t total_weight = cpu->rq_weight;
  if (running)
    total_weight += thread_weight(running);
  if (!total_weight)
    return SCHEDULER_TARGET_LATENCY_MS;

  uint64_t slice =
      (SCHEDULER_TARGET_LATENCY_MS * thread_weight(thread)) / total_weight;
  if (!slice)
    slice = 1;
  if (slice > SCHEDULER_TARGET_LATENCY_MS)
    slice = SCHEDULER_TARGET_LATENCY_MS;
  return (uint32_t)slice;
}

static uint64_t virtual_deadline_after(const struct thread *thread,
                                       uint32_t slice_ms)
{
  uint64_t weight = scheduler_weight(thread->qos);
  uint64_t delta = ((uint64_t)slice_ms * SCHEDULER_NICE_0_WEIGHT) / weight;
  if (!delta)
    delta = 1;
  return UINT64_MAX - thread->virtual_runtime < delta
             ? UINT64_MAX
             : thread->virtual_runtime + delta;
}

static void ensure_fair_request(const struct scheduler_cpu *cpu,
                                struct thread *thread,
                                const struct thread *running)
{
  if (thread->idle || thread->ticks_remaining)
    return;
  thread->ticks_remaining = fair_slice_ms(cpu, thread, running);
  thread->virtual_deadline =
      virtual_deadline_after(thread, thread->ticks_remaining);
}

static void thread_trampoline(void);
static uint64_t create_initial_stack(struct thread *thread);
static bool kernel_text_address_valid(uint64_t address)
{
  return address >= 0xffffffff80000000ULL;
}

static bool thread_stack_valid(const struct thread *thread)
{
  if (!thread)
    return false;
  if (thread->rsp & 0x7ULL)
    return false;
  if (thread->rsp < 0xffff800000000000ULL)
    return false;
  if (thread->idle)
    return true;
  if (!thread->kstack)
    return false;
  uint64_t stack_base = (uint64_t)(uintptr_t)thread->kstack;
  uint64_t stack_top = stack_base + SCHEDULER_STACK_SIZE;
  if (thread->rsp >= stack_base + 64 && thread->rsp < stack_top)
    return true;
  return false;
}

static bool thread_stack_return_valid(uint64_t rsp)
{
  if (!rsp)
    return false;
  uint64_t *slot = (uint64_t *)rsp;
  uint64_t return_address = slot[6];
  if (!kernel_text_address_valid(return_address))
    return false;
  return true;
}

static void write_hex_digits(char *out, uint64_t value, int digits)
{
  static const char hexdigits[] = "0123456789abcdef";
  for (int i = digits - 1; i >= 0; i--)
  {
    out[digits - 1 - i] = hexdigits[(value >> (i * 4)) & 0xF];
  }
}

static char sched_panic_reason[224];

static void validate_switch_target(const struct thread *prev,
                                   const struct thread *next)
{
  if (!next)
  {
    kernel_panic("scheduler: null next thread");
  }
  if (!thread_stack_valid(next))
  {
    char *p = sched_panic_reason;
    const char *prefix = "sched: bad RSP tgt=0x";
    for (int i = 0; prefix[i]; i++)
      *p++ = prefix[i];
    write_hex_digits(p, next->id, 8);
    p += 8;
    const char *mid = " rsp=0x";
    for (int i = 0; mid[i]; i++)
      *p++ = mid[i];
    write_hex_digits(p, next->rsp, 16);
    p += 16;
    const char *mid2 = " base=0x";
    for (int i = 0; mid2[i]; i++)
      *p++ = mid2[i];
    write_hex_digits(p, (uint64_t)(uintptr_t)next->kstack, 16);
    p += 16;
    const char *mid3 = " prev=0x";
    for (int i = 0; mid3[i]; i++)
      *p++ = mid3[i];
    write_hex_digits(p, prev ? prev->id : 0xFFFFFFFFu, 8);
    p += 8;
    *p = '\0';
    kernel_panic(sched_panic_reason);
  }
  if (!thread_stack_return_valid(next->rsp))
  {
    uint64_t *slot = (uint64_t *)next->rsp;
    uint64_t ret = slot[6];
    char *p = sched_panic_reason;
    const char *prefix = "sched: bad return 0x";
    for (int i = 0; prefix[i]; i++)
      *p++ = prefix[i];
    write_hex_digits(p, ret, 16);
    p += 16;
    const char *mid = " tgt=0x";
    for (int i = 0; mid[i]; i++)
      *p++ = mid[i];
    write_hex_digits(p, next->id, 8);
    p += 8;
    const char *mid2 = " prev=0x";
    for (int i = 0; mid2[i]; i++)
      *p++ = mid2[i];
    write_hex_digits(p, prev ? prev->id : 0xFFFFFFFFu, 8);
    p += 8;
    *p = '\0';
    kernel_panic(sched_panic_reason);
  }
}

#define MSR_FS_BASE 0xC0000100U
static inline void write_fs_base(uint64_t value)
{
#ifndef PUREC_HOST_TEST
  __asm__ volatile("wrmsr"
                   :
                   : "c"(MSR_FS_BASE), "a"((uint32_t)value),
                     "d"((uint32_t)(value >> 32))
                   : "memory");
#else
  (void)value;
#endif
}

void scheduler_set_fs_base(uint64_t base)
{
  struct thread *t = scheduler_current_thread();
  if (!t)
    return;
  uint64_t flags = irq_save();
  t->fs_base = base;
  write_fs_base(base);
  irq_restore(flags);
}

static void thread_node_free(struct thread *t);
static void finish_switch(void)
{
  struct scheduler_cpu *cpu = local_scheduler();
  struct thread *incoming = cpu->current;
  if (incoming && incoming->user_mode)
    write_fs_base(incoming->fs_base);
  struct thread *previous = cpu->previous;
  struct thread *victim = NULL;
  if (previous)
  {
    previous->running_cpu = -1;
    if (previous->state == THREAD_TERMINATED && !previous->idle)
    {
      struct thread **link = &thread_list;
      while (*link && *link != previous)
        link = &(*link)->next;
      if (*link)
      {
        *link = previous->next;
        thread_live_count--;
        thash_remove(previous);
        victim = previous;
      }
    }
  }
  cpu->previous = NULL;
  unlock_and_kick(cpu);
  if (victim)
    thread_node_free(victim);
}

static void thread_trampoline(void)
{
  finish_switch();
  struct thread *self = scheduler_current_thread();
  if (!self->user_mode)
    __asm__ volatile("sti" ::: "memory");
  self->entry(self->arg);
  scheduler_exit();
  __builtin_unreachable();
}

static uint64_t create_initial_stack(struct thread *thread)
{
  uint64_t *sp = (uint64_t *)(thread->kstack + SCHEDULER_STACK_SIZE);
  *--sp = 0;
  *--sp = (uint64_t)thread_trampoline;
  for (unsigned i = 0; i < 6; i++)
    *--sp = 0;
  return (uint64_t)sp;
}

void scheduler_init_cpu(void)
{
  uint32_t id = gdt_current_cpu_id();
  struct scheduler_cpu *cpu = local_scheduler();
  if (cpu->initialized)
    return;
  struct thread *idle = &idle_threads[id];
  memset(idle, 0, sizeof(*idle));
  idle->idle = true;
  idle->state = THREAD_RUNNING;
  idle->rq_cpu = -1;
  idle->last_cpu = (int16_t)id;
  idle->running_cpu = (int16_t)id;
  idle->affinity = (int16_t)id;
  idle->qos = SCHED_QOS_IDLE;
  idle->address_space = vmm_kernel_address_space();
  idle->ticks_remaining = 0;
  strncpy(idle->name, "idle", sizeof(idle->name) - 1);
  fpu_thread_init(idle->fpu_state);
  cpu->idle = idle;
  cpu->current = idle;
  sched_publish_current(id, idle);
  cpu->last_tick = timer_ticks();
  cpu->initialized = true;
}

void scheduler_init(void)
{
  if (gdt_current_cpu_id() != 0 || local_scheduler()->initialized)
    return;
  thread_list = NULL;
  thread_live_count = 0;
  sleep_head = NULL;
  for (unsigned i = 0; i < THASH_SIZE; i++)
    thash_buckets[i] = NULL;
  scheduler_init_cpu();
  klogf(KLOG_OK, "sched: dynamic runqueue, limit=RAM");
}

static struct thread *thread_node_alloc(void)
{
  struct thread *t = (struct thread *)oom_slab_alloc(sizeof(struct thread));
  uint64_t node_phys = 0;
  if (!t)
  {
    node_phys = pmm_allocate_page();
    if (!node_phys)
      return NULL;
    t = (struct thread *)pmm_physical_to_virtual(node_phys);
  }
  uint64_t stack_phys = pmm_allocate_contiguous(SCHEDULER_STACK_PAGES);
  if (!stack_phys)
  {
    if (node_phys)
      pmm_free_page(node_phys);
    else
      oom_slab_free(t, sizeof(struct thread));
    return NULL;
  }
  memset(t, 0, sizeof(*t));
  t->node_phys = node_phys;
  t->kstack_phys = stack_phys;
  t->kstack = (uint8_t *)pmm_physical_to_virtual(stack_phys);
  memset(t->kstack, 0, SCHEDULER_STACK_SIZE);
  return t;
}

static void thread_node_free(struct thread *t)
{
  if (!t || t->idle)
    return;
  uint64_t kstack_phys = t->kstack_phys;
  uint64_t node_phys = t->node_phys;
  if (kstack_phys)
    pmm_free_contiguous(kstack_phys, SCHEDULER_STACK_PAGES);
  if (node_phys)
    pmm_free_page(node_phys);
  else
    oom_slab_free(t, sizeof(struct thread));
}

#ifdef PUREC_HOST_TEST
void scheduler_host_reset(void)
{
  uint64_t flags = spin_lock_irqsave(&runqueue_lock);
  struct thread *t = thread_list;
  thread_list = NULL;
  thread_live_count = 0;
  next_id = 1;
  active_mask = 0;
  sleep_head = NULL;
  for (unsigned i = 0; i < THASH_SIZE; i++)
    thash_buckets[i] = NULL;
  memset(cpu_schedulers, 0, sizeof(cpu_schedulers));
  memset(idle_threads, 0, sizeof(idle_threads));
  spin_unlock_irqrestore(&runqueue_lock, flags);
  while (t)
  {
    struct thread *n = t->next;
    thread_node_free(t);
    t = n;
  }
}

struct thread *scheduler_host_lookup(int tid)
{
  if (tid < 0)
    return NULL;
  uint64_t flags = spin_lock_irqsave(&runqueue_lock);
  struct thread *found = thash_find((uint32_t)tid);
  spin_unlock_irqrestore(&runqueue_lock, flags);
  return found;
}

void scheduler_host_sleep_thread(struct thread *thread, uint64_t wake_tick)
{
  uint64_t flags = spin_lock_irqsave(&runqueue_lock);
  struct scheduler_cpu *cpu =
      &cpu_schedulers[thread->rq_cpu >= 0 ? thread->rq_cpu : 0];
  thread_block_locked(thread, cpu, wake_tick);
  spin_unlock_irqrestore(&runqueue_lock, flags);
}

void scheduler_host_requeue(struct thread *thread, uint32_t cpu_id)
{
  uint64_t flags = spin_lock_irqsave(&runqueue_lock);
  requeue_prev(thread, cpu_id, &cpu_schedulers[cpu_id]);
  spin_unlock_irqrestore(&runqueue_lock, flags);
}

void scheduler_host_dequeue(struct thread *thread)
{
  uint64_t flags = spin_lock_irqsave(&runqueue_lock);
  rq_dequeue(thread);
  spin_unlock_irqrestore(&runqueue_lock, flags);
}

struct thread *scheduler_host_idle(uint32_t id)
{
  if (id >= CPU_MAX_COUNT)
    return NULL;
  return &idle_threads[id];
}
#endif

static struct thread *alloc_thread(void)
{
  for (struct thread *t = thread_list; t; t = t->next)
  {
    if ((t->state == THREAD_FREE || t->state == THREAD_TERMINATED) &&
        t->running_cpu == -1)
      return t;
  }
  struct thread *fresh = thread_node_alloc();
  if (!fresh)
    return NULL;
  fresh->next = thread_list;
  thread_list = fresh;
  thread_live_count++;
  return fresh;
}

void scheduler_free_thread_by_id(int tid)
{
  if (tid < 0)
    return;
  uint64_t flags = spin_lock_irqsave(&runqueue_lock);
  struct thread *victim = thash_find((uint32_t)tid);
  if (victim && (victim->state != THREAD_TERMINATED ||
                 victim->running_cpu != -1 || victim->idle))
    victim = NULL;
  if (victim)
  {
    struct thread **link = &thread_list;
    while (*link && *link != victim)
      link = &(*link)->next;
    if (*link)
    {
      *link = victim->next;
      thread_live_count--;
      thash_remove(victim);
    }
    else
    {
      victim = NULL;
    }
  }
  spin_unlock_irqrestore(&runqueue_lock, flags);
  if (victim)
    thread_node_free(victim);
}

static int create_thread(void (*entry)(void *), void *arg, const char *name,
                         uint8_t priority, int16_t affinity,
                         uint64_t address_space, struct process *process,
                         bool user_mode, const void *fpu_image,
                         const uint64_t *frame, uint64_t fs_base,
                         uint64_t clear_child_tid)
{
  if (!local_scheduler()->initialized || !entry)
    return -1;
  if (affinity < -1 || affinity >= (int16_t)cpu_registered_count())
    return -1;
  if (affinity >= 0 && !cpu_is_online((uint32_t)affinity))
    return -1;
  uint64_t flags = spin_lock_irqsave(&runqueue_lock);
  struct thread *t = alloc_thread();
  if (!t)
  {
    spin_unlock_irqrestore(&runqueue_lock, flags);
    return -1;
  }
  thash_remove(t);
  uint8_t *kstack = t->kstack;
  uint64_t kstack_phys = t->kstack_phys;
  uint64_t node_phys = t->node_phys;
  struct thread *next = t->next;
  memset(t, 0, sizeof(*t));
  t->kstack = kstack;
  t->kstack_phys = kstack_phys;
  t->node_phys = node_phys;
  t->next = next;
  t->id = next_id++;
  if (t->id == 0)
    t->id = next_id++;
  for (;;)
  {
    if (!thash_find(t->id))
      break;
    if (++next_id == 0)
      next_id = 1;
    t->id = next_id++;
  }
  thash_insert(t);
  t->entry = entry;
  t->arg = arg;
  t->priority = priority > 7 ? 7 : priority;
  t->qos = SCHED_QOS_DEFAULT;
  t->affinity = affinity;
  t->running_cpu = -1;
  t->kernel_only = user_mode;
  t->ticks_remaining = 0;
  t->virtual_runtime = 0;
  t->rq_cpu = -1;
  t->last_cpu = -1;
  t->virtual_deadline = 0;
  t->address_space = address_space ? address_space : vmm_kernel_address_space();
  t->process = process;
  t->user_mode = user_mode;
  strncpy(t->name, name ? name : "thread", sizeof(t->name) - 1);
  t->fs_base = fs_base;
  t->clear_child_tid = clear_child_tid;
  if (frame)
    memcpy(t->user_frame, frame, sizeof(t->user_frame));
  t->rsp = create_initial_stack(t);
  if (fpu_image)
    memcpy(t->fpu_state, fpu_image, FPU_STATE_SIZE);
  else
    fpu_thread_init(t->fpu_state);
  int id = (int)t->id;
  struct scheduler_cpu *local = local_scheduler();
  make_runnable(t, gdt_current_cpu_id(), local);
  unlock_and_kick(local);
  irq_restore(flags);
  return id;
}

void scheduler_set_qos(int tid, uint8_t qos)
{
  if (qos > SCHED_QOS_USER_INTERACTIVE)
    return;
  uint64_t flags = spin_lock_irqsave(&runqueue_lock);
  struct thread *found = (tid >= 0) ? thash_find((uint32_t)tid) : NULL;
  if (found && found->state != THREAD_FREE)
  {
    int16_t queued = found->rq_cpu;
    if (queued >= 0)
      rq_dequeue(found);
    found->qos = qos;
    found->ticks_remaining = 0;
    found->virtual_deadline = 0;
    if (queued >= 0)
      rq_enqueue((uint32_t)queued, found);
  }
  spin_unlock_irqrestore(&runqueue_lock, flags);
  smp_reschedule_all();
}

uint8_t scheduler_get_qos(int tid)
{
  uint64_t flags = spin_lock_irqsave(&runqueue_lock);
  struct thread *found = (tid >= 0) ? thash_find((uint32_t)tid) : NULL;
  uint8_t qos = found ? found->qos : SCHED_QOS_DEFAULT;
  spin_unlock_irqrestore(&runqueue_lock, flags);
  return qos;
}

int scheduler_create_thread(void (*entry)(void *), void *arg, const char *name,
                            uint8_t priority, int16_t affinity)
{
  return create_thread(entry, arg, name, priority, affinity,
                       vmm_kernel_address_space(), NULL, false, NULL, NULL, 0, 0);
}
int scheduler_create_user_thread(void (*entry)(void *), void *arg,
                                 const char *name, uint8_t priority,
                                 int16_t affinity, uint64_t address_space,
                                 struct process *process)
{
  if (!address_space || !process)
    return -1;
  return create_thread(entry, arg, name, priority, affinity, address_space,
                       process, true, NULL, NULL, 0, 0);
}

int scheduler_create_forked_thread(void (*entry)(void *arg), void *arg,
                                   const char *name, uint8_t priority,
                                   int16_t affinity, uint64_t address_space,
                                   struct process *process)
{
  if (!address_space || !process)
    return -1;
  uint8_t fpu_image[FPU_STATE_SIZE] __attribute__((aligned(16)));
  fpu_save(fpu_image);
  struct thread *parent = scheduler_current_thread();
  return create_thread(entry, arg, name, priority, affinity, address_space,
                       process, true, fpu_image, NULL,
                       parent ? parent->fs_base : 0, 0);
}

int scheduler_create_sibling_thread(void (*entry)(void *), void *arg,
                                    const char *name, uint8_t priority,
                                    uint64_t address_space,
                                    struct process *process,
                                    const uint64_t *frame, uint64_t fs_base,
                                    uint64_t clear_child_tid)
{
  if (!address_space || !process || !frame)
    return -1;
  return create_thread(entry, arg, name, priority, -1, address_space, process,
                       true, NULL, frame, fs_base, clear_child_tid);
}

int scheduler_create_service_thread(void (*entry)(void *), void *arg,
                                    const char *name, uint8_t priority,
                                    int16_t affinity, uint64_t address_space,
                                    struct process *process)
{
  if (!process)
    return -1;
  return create_thread(entry, arg, name, priority, affinity, address_space,
                       process, false, NULL, NULL, 0, 0);
}

struct thread *scheduler_current_thread(void)
{
#ifndef PUREC_HOST_TEST
  return percpu_current();
#else
  uint64_t flags = irq_save();
  uint32_t id = gdt_current_cpu_id();
  struct thread *t = id < CPU_MAX_COUNT ? cpu_schedulers[id].current : NULL;
  irq_restore(flags);
  return t;
#endif
}
int scheduler_current_tid(void)
{
  struct thread *t = scheduler_current_thread();
  return t ? (int)t->id : -1;
}
uint32_t scheduler_active_mask(void)
{
  return __atomic_load_n(&active_mask, __ATOMIC_ACQUIRE);
}
bool scheduler_is_running(void)
{
  uint32_t id = gdt_current_cpu_id();
  return id < CPU_MAX_COUNT && cpu_schedulers[id].started;
}
int scheduler_get_core_count(void)
{
  uint32_t mask = scheduler_active_mask();
  int count = 0;
  while (mask)
  {
    count += (int)(mask & 1);
    mask >>= 1;
  }
  return count;
}

uint32_t scheduler_thread_count(void)
{
  uint64_t flags = spin_lock_irqsave(&runqueue_lock);
  uint32_t count = 0;
  for (struct thread *t = thread_list; t; t = t->next)
    if (t->state != THREAD_FREE && t->state != THREAD_TERMINATED)
      count++;
  spin_unlock_irqrestore(&runqueue_lock, flags);
  return count;
}
uint64_t scheduler_thread_runtime_ticks(int tid)
{
  uint64_t flags = spin_lock_irqsave(&runqueue_lock);
  uint64_t result = 0;
  struct thread *found = (tid > 0) ? thash_find((uint32_t)tid) : NULL;
  if (found)
    result = found->runtime_ticks;
  spin_unlock_irqrestore(&runqueue_lock, flags);
  return result;
}
bool scheduler_thread_stopped(int tid)
{
  uint64_t flags = spin_lock_irqsave(&runqueue_lock);
  bool stopped = true;
  struct thread *found = (tid >= 0) ? thash_find((uint32_t)tid) : NULL;
  if (found)
    stopped = found->state == THREAD_TERMINATED && found->running_cpu == -1;
  spin_unlock_irqrestore(&runqueue_lock, flags);
  return stopped;
}

uint64_t scheduler_total_ticks(void)
{
  uint64_t total = 0;
  for (unsigned i = 0; i < CPU_MAX_COUNT; i++)
    total += __atomic_load_n(&cpu_schedulers[i].total_ticks, __ATOMIC_RELAXED);
  return total;
}
uint64_t scheduler_idle_ticks(void)
{
  uint64_t total = 0;
  for (unsigned i = 0; i < CPU_MAX_COUNT; i++)
    total += __atomic_load_n(&cpu_schedulers[i].idle_ticks, __ATOMIC_RELAXED);
  return total;
}
bool scheduler_cpu_ticks(uint32_t id, uint64_t *total, uint64_t *idle)
{
  if (id >= CPU_MAX_COUNT)
    return false;
  if (total)
    *total = __atomic_load_n(&cpu_schedulers[id].total_ticks, __ATOMIC_RELAXED);
  if (idle)
    *idle = __atomic_load_n(&cpu_schedulers[id].idle_ticks, __ATOMIC_RELAXED);
  return true;
}

void scheduler_set_affinity(int tid, int16_t core)
{
  if (core < -1 || core >= (int16_t)cpu_registered_count())
    return;
  if (core >= 0 && !cpu_is_online((uint32_t)core))
    return;
  uint64_t flags = spin_lock_irqsave(&runqueue_lock);
  struct thread *found = (tid >= 0) ? thash_find((uint32_t)tid) : NULL;
  if (found && found->state != THREAD_FREE)
  {
    found->affinity = core;
    found->affinity_auto = false;
    if (found->rq_cpu >= 0 && !eligible(found, (uint32_t)found->rq_cpu))
    {
      struct scheduler_cpu *source = &cpu_schedulers[found->rq_cpu];
      uint64_t relative = vruntime_relative(found, source);
      rq_dequeue(found);
      uint32_t target = select_cpu(found, (uint32_t)core < CPU_MAX_COUNT ? (uint32_t)core : 0);
      found->virtual_runtime =
          saturating_add(cpu_schedulers[target].rq_min_vruntime, relative);
      found->ticks_remaining = 0;
      found->virtual_deadline = 0;
      rq_enqueue(target, found);
    }
  }
  spin_unlock_irqrestore(&runqueue_lock, flags);
  smp_reschedule_all();
}

static void sleep_insert(struct thread *thread)
{
  struct thread **link = &sleep_head;
  while (*link && (*link)->wake_tick <= thread->wake_tick)
    link = &(*link)->sleep_next;
  thread->sleep_next = *link;
  *link = thread;
}

static void sleep_remove(struct thread *thread)
{
  struct thread **link = &sleep_head;
  while (*link && *link != thread)
    link = &(*link)->sleep_next;
  if (*link)
    *link = thread->sleep_next;
  thread->sleep_next = NULL;
}

static void thread_block_locked(struct thread *thread,
                                struct scheduler_cpu *cpu, uint64_t wake_tick)
{
  if (thread->rq_cpu >= 0)
    rq_dequeue(thread);
  thread->virtual_runtime = vruntime_relative(thread, cpu);
  thread->state = THREAD_BLOCKED;
  thread->wake_tick = wake_tick;
  if (wake_tick)
    sleep_insert(thread);
}

static void make_runnable(struct thread *thread, uint32_t here,
                          struct scheduler_cpu *local)
{
  uint32_t target = select_cpu(thread, here);
  struct scheduler_cpu *dst = &cpu_schedulers[target];
  thread->virtual_runtime =
      saturating_add(dst->rq_min_vruntime, thread->virtual_runtime);
  thread->ticks_remaining = 0;
  thread->virtual_deadline = 0;
  thread->wake_tick = 0;
  thread->state = THREAD_READY;
  rq_enqueue(target, thread);
  if (target != here)
    local->pending_kick |= 1U << target;
}

static void wake_expired_sleepers(struct scheduler_cpu *local, uint32_t here)
{
  uint64_t now = timer_ticks();
  while (sleep_head && sleep_head->wake_tick <= now)
  {
    struct thread *thread = sleep_head;
    sleep_head = thread->sleep_next;
    thread->sleep_next = NULL;
    if (thread->state == THREAD_BLOCKED)
      make_runnable(thread, here, local);
    else
      thread->wake_tick = 0;
  }
}

static void rq_update_min(struct scheduler_cpu *cpu,
                          const struct thread *running)
{
  uint64_t minimum = running ? running->virtual_runtime : UINT64_MAX;
  for (const struct thread *t = cpu->rq_head; t; t = t->rq_next)
  {
    if (t->virtual_runtime < minimum)
      minimum = t->virtual_runtime;
  }
  if (minimum != UINT64_MAX && minimum > cpu->rq_min_vruntime)
    cpu->rq_min_vruntime = minimum;
}

static uint64_t rq_avg_vruntime(const struct scheduler_cpu *cpu,
                                const struct thread *running)
{
  uint64_t total_weight = 0;
  uint64_t weighted_delta = 0;
  const struct thread *t = running ? running : cpu->rq_head;
  while (t)
  {
    uint64_t weight = thread_weight(t);
    uint64_t delta = t->virtual_runtime > cpu->rq_min_vruntime
                         ? t->virtual_runtime - cpu->rq_min_vruntime
                         : 0;
    if (delta && weight > (UINT64_MAX - weighted_delta) / delta)
      weighted_delta = UINT64_MAX;
    else if (weighted_delta != UINT64_MAX)
      weighted_delta += delta * weight;
    total_weight += weight;
    t = (t == running) ? cpu->rq_head : t->rq_next;
  }
  if (!total_weight)
    return cpu->rq_min_vruntime;
  return saturating_add(cpu->rq_min_vruntime, weighted_delta / total_weight);
}

struct pick_state
{
  struct thread *best_eligible;
  struct thread *best_fallback;
  uint64_t average;
};

static void pick_consider(struct pick_state *state,
                          const struct scheduler_cpu *cpu, struct thread *t,
                          uint32_t id, const struct thread *running)
{
  if (!eligible(t, id))
    return;
  if (t->rq_cpu >= 0 && (t->state != THREAD_READY || t->running_cpu != -1))
    return;
  ensure_fair_request(cpu, t, running);
  struct thread *fallback = state->best_fallback;
  if (!fallback || t->virtual_runtime < fallback->virtual_runtime ||
      (t->virtual_runtime == fallback->virtual_runtime &&
       t->virtual_deadline < fallback->virtual_deadline))
    state->best_fallback = t;
  if (t->virtual_runtime <= state->average &&
      (!state->best_eligible ||
       t->virtual_deadline < state->best_eligible->virtual_deadline))
    state->best_eligible = t;
}

static struct thread *rq_select(struct scheduler_cpu *cpu, uint32_t id,
                                struct thread *running)
{
  if (!running && !cpu->rq_head)
    return NULL;
  rq_update_min(cpu, running);
  struct pick_state state = {NULL, NULL, rq_avg_vruntime(cpu, running)};
  if (running)
    pick_consider(&state, cpu, running, id, running);
  for (struct thread *t = cpu->rq_head; t; t = t->rq_next)
    pick_consider(&state, cpu, t, id, running);
  return state.best_eligible ? state.best_eligible : state.best_fallback;
}

static struct thread *pull_task(uint32_t dst_id, bool idle_pull)
{
  struct scheduler_cpu *dst = &cpu_schedulers[dst_id];
  uint32_t count = cpu_registered_count();
  if (count > CPU_MAX_COUNT)
    count = CPU_MAX_COUNT;
  uint64_t dst_load = cpu_load(dst_id);
  uint64_t now = timer_ticks();
  struct thread *victim = NULL;
  uint32_t victim_cpu = CPU_MAX_COUNT;
  uint64_t victim_load = 0;
  for (uint32_t id = 0; id < count; id++)
  {
    if (id == dst_id || !cpu_schedulers[id].rq_count)
      continue;
    uint64_t load = cpu_load(id);
    struct thread *candidate = NULL;
    bool stranded = false;
    for (struct thread *t = cpu_schedulers[id].rq_head; t; t = t->rq_next)
    {
      if (!eligible(t, dst_id) || t->state != THREAD_READY ||
          t->running_cpu != -1)
        continue;
      bool lost = !eligible(t, id);
      if (!lost && (load <= dst_load ||
                    (!idle_pull && now - t->last_ran < SCHEDULER_CACHE_HOT_MS)))
        continue;
      if (lost && !stranded)
      {
        candidate = NULL;
        stranded = true;
      }
      if (stranded && !lost)
        continue;
      if (!candidate || t->virtual_runtime > candidate->virtual_runtime)
        candidate = t;
    }
    if (!candidate)
      continue;
    if (!stranded && load <= dst_load + thread_weight(candidate))
      continue;
    if (stranded)
      load = UINT64_MAX;
    if (!victim || load > victim_load)
    {
      victim = candidate;
      victim_cpu = id;
      victim_load = load;
    }
  }
  if (!victim)
    return NULL;
  uint64_t relative = vruntime_relative(victim, &cpu_schedulers[victim_cpu]);
  rq_dequeue(victim);
  victim->virtual_runtime = saturating_add(dst->rq_min_vruntime, relative);
  victim->ticks_remaining = 0;
  victim->virtual_deadline = 0;
  rq_enqueue(dst_id, victim);
  return victim;
}

static struct thread *pick_next(void)
{
  uint32_t id = gdt_current_cpu_id();
  struct scheduler_cpu *cpu = local_scheduler();
  wake_expired_sleepers(cpu, id);
  struct thread *running = running_candidate(cpu);
  struct thread *chosen = rq_select(cpu, id, running);
  if (!chosen)
  {
    chosen = pull_task(id, true);
    if (chosen)
      ensure_fair_request(cpu, chosen, running);
  }
  return chosen ? chosen : cpu->idle;
}

static void account_current(struct scheduler_cpu *cpu, uint64_t now)
{
  if (!cpu || !cpu->started)
    return;
  uint64_t elapsed = now - cpu->last_tick;
  cpu->last_tick = now;
  if (!elapsed)
    return;

  __atomic_fetch_add(&cpu->total_ticks, elapsed, __ATOMIC_RELAXED);
  struct thread *current = cpu->current;
  if (!current)
    return;
  if (current->idle)
  {
    __atomic_fetch_add(&cpu->idle_ticks, elapsed, __ATOMIC_RELAXED);
    return;
  }

  current->runtime_ticks += elapsed;
  uint64_t weight = scheduler_weight(current->qos);
  uint64_t delta = elapsed > UINT64_MAX / SCHEDULER_NICE_0_WEIGHT
                       ? UINT64_MAX
                       : (elapsed * SCHEDULER_NICE_0_WEIGHT) / weight;
  if (!delta)
    delta = 1;
  current->virtual_runtime =
      UINT64_MAX - current->virtual_runtime < delta
          ? UINT64_MAX
          : current->virtual_runtime + delta;
  if (elapsed >= current->ticks_remaining)
    current->ticks_remaining = 0;
  else
    current->ticks_remaining -= (uint32_t)elapsed;
}

static void warn_sleep_with_preempt_off(void)
{
#ifndef PUREC_HOST_TEST
  static volatile bool busy;
  static uint32_t reported;
  if (reported >= 16 || __atomic_exchange_n(&busy, true, __ATOMIC_ACQUIRE))
    return;
  reported++;
  klog(KLOG_WARN, "sched: blocking with preemption disabled");
  uintptr_t *frame = (uintptr_t *)__builtin_frame_address(0);
  for (int i = 0; i < 4; i++)
  {
    if ((uintptr_t)frame < 0xffff800000000000ULL || ((uintptr_t)frame & 7))
      break;
    uintptr_t next = frame[0];
    uint64_t addr = frame[1];
    const char *name = 0;
    uint64_t off = 0;
    if (addr >= 0xffffffff80000000ULL && ksym_resolve(addr, &name, &off))
      klogf(KLOG_WARN, "  caller #%d [<%016llx>] %s+0x%llx", i,
            (unsigned long long)addr, name, (unsigned long long)off);
    if (next <= (uintptr_t)frame)
      break;
    frame = (uintptr_t *)next;
  }
  __atomic_store_n(&busy, false, __ATOMIC_RELEASE);
#endif
}

static void requeue_prev(struct thread *prev, uint32_t id,
                         struct scheduler_cpu *cpu)
{
  prev->state = THREAD_READY;
  if (prev->idle)
    return;
  uint32_t target = eligible(prev, id) ? id : select_cpu(prev, id);
  if (target != id)
  {
    prev->virtual_runtime =
        saturating_add(cpu_schedulers[target].rq_min_vruntime,
                       vruntime_relative(prev, cpu));
    prev->ticks_remaining = 0;
    prev->virtual_deadline = 0;
    cpu->pending_kick |= 1U << target;
  }
  rq_enqueue(target, prev);
}

static void schedule_locked(void)
{
  struct scheduler_cpu *cpu = local_scheduler();
  struct thread *prev = cpu->current;
  struct thread *next = pick_next();
  uint32_t id = gdt_current_cpu_id();
  if (next == prev)
  {
    prev->state = THREAD_RUNNING;
    unlock_and_kick(cpu);
    return;
  }
  if (prev->state == THREAD_RUNNING)
    requeue_prev(prev, id, cpu);
  prev->last_ran = timer_ticks();
  prev->last_cpu = (int16_t)id;
  rq_dequeue(next);
  next->state = THREAD_RUNNING;
  if (next->cpu_mask && !(next->cpu_mask & (1U << id)))
    next->migrations++;
  next->cpu_mask |= 1U << id;
  next->running_cpu = (int16_t)id;
  cpu->previous = prev;
  cpu->current = next;
  sched_publish_current(id, next);
  prev->preempt_saved = cpu->preempt_depth;
  cpu->preempt_depth = next->preempt_saved;
  validate_switch_target(prev, next);
  fpu_save(prev->fpu_state);
  gdt_set_kernel_stack(
      (uint64_t)(uintptr_t)(next->kstack + SCHEDULER_STACK_SIZE));
  vmm_switch_address_space(next->address_space);
  fpu_restore(next->fpu_state);
  scheduler_asm_switch(&prev->rsp, &next->rsp);
  finish_switch();
}

void scheduler_yield(void)
{
  uint64_t flags = irq_save();
  struct scheduler_cpu *cpu = local_scheduler();
  if (!cpu->initialized)
  {
    irq_restore(flags);
    return;
  }
  if (cpu->preempt_depth)
  {
    cpu->reschedule_pending = true;
    irq_restore(flags);
    return;
  }
  spin_lock(&runqueue_lock);
  account_current(cpu, timer_ticks());
  schedule_locked();
  irq_restore(flags);
}
bool scheduler_preempt_disable(void)
{
  uint64_t flags = irq_save();
  uint32_t id = gdt_current_cpu_id();
  if (id >= CPU_MAX_COUNT)
  {
    irq_restore(flags);
    return false;
  }
  struct scheduler_cpu *cpu = &cpu_schedulers[id];
  if (!cpu->initialized || !cpu->started)
  {
    irq_restore(flags);
    return false;
  }
  cpu->preempt_depth++;
  irq_restore(flags);
  return true;
}
void scheduler_preempt_enable(void)
{
  uint64_t flags = irq_save();
  struct scheduler_cpu *cpu = local_scheduler();
  if (!cpu->initialized)
  {
    irq_restore(flags);
    return;
  }
  if (!cpu->preempt_depth)
  {
    irq_restore(flags);
    kernel_panic("sched: unbalanced preempt enable");
  }
  cpu->preempt_depth--;
  bool schedule =
      cpu->preempt_depth == 0 && cpu->reschedule_pending && cpu->started;
  if (schedule)
    cpu->reschedule_pending = false;
  irq_restore(flags);
  if (schedule)
    scheduler_yield();
}
void scheduler_sleep(uint32_t milliseconds)
{
  if (!milliseconds)
  {
    scheduler_yield();
    return;
  }
  if (!local_scheduler()->initialized)
    return;
  if (local_scheduler()->preempt_depth)
    warn_sleep_with_preempt_off();
  uint64_t flags = spin_lock_irqsave(&runqueue_lock);
  struct thread *t = local_scheduler()->current;
  if (t->idle)
  {
    spin_unlock_irqrestore(&runqueue_lock, flags);
    return;
  }
  account_current(local_scheduler(), timer_ticks());
  uint64_t now = timer_ticks();
  uint64_t wake_tick = now + milliseconds;
  if (wake_tick < now)
    wake_tick = UINT64_MAX;
  thread_block_locked(t, local_scheduler(), wake_tick);
  t->wake_pending = false;
  schedule_locked();
  irq_restore(flags);
}
void scheduler_block(void)
{
  if (!local_scheduler()->initialized)
    return;
  if (local_scheduler()->preempt_depth)
    warn_sleep_with_preempt_off();
  uint64_t flags = spin_lock_irqsave(&runqueue_lock);
  struct thread *t = local_scheduler()->current;
  if (t->idle || t->wake_pending)
  {
    t->wake_pending = false;
    spin_unlock_irqrestore(&runqueue_lock, flags);
    return;
  }
  account_current(local_scheduler(), timer_ticks());
  thread_block_locked(t, local_scheduler(), 0);
  schedule_locked();
  irq_restore(flags);
}
void scheduler_block_timeout(uint32_t milliseconds)
{
  if (!local_scheduler()->initialized)
    return;
  if (!milliseconds)
  {
    scheduler_block();
    return;
  }
  if (local_scheduler()->preempt_depth)
    warn_sleep_with_preempt_off();
  uint64_t flags = spin_lock_irqsave(&runqueue_lock);
  struct thread *t = local_scheduler()->current;
  if (t->idle || t->wake_pending)
  {
    t->wake_pending = false;
    spin_unlock_irqrestore(&runqueue_lock, flags);
    return;
  }
  account_current(local_scheduler(), timer_ticks());
  uint64_t now = timer_ticks();
  uint64_t wake_tick = now + milliseconds;
  if (wake_tick < now)
    wake_tick = UINT64_MAX;
  thread_block_locked(t, local_scheduler(), wake_tick);
  schedule_locked();
  irq_restore(flags);
}

void scheduler_wake_process_threads(const struct process *process,
                                    int skip_tid)
{
  uint64_t flags = spin_lock_irqsave(&runqueue_lock);
  struct scheduler_cpu *local = local_scheduler();
  for (struct thread *t = thread_list; t; t = t->next)
  {
    if (t->process != process || (int)t->id == skip_tid || t->idle)
      continue;
    if (t->state == THREAD_BLOCKED)
    {
      if (t->wake_tick)
        sleep_remove(t);
      make_runnable(t, gdt_current_cpu_id(), local);
    }
    else if (t->state == THREAD_RUNNING || t->state == THREAD_READY)
    {
      t->wake_pending = true;
    }
  }
  unlock_and_kick(local);
  irq_restore(flags);
}

void scheduler_unblock(int tid)
{
  uint64_t flags = spin_lock_irqsave(&runqueue_lock);
  struct scheduler_cpu *local = local_scheduler();
  struct thread *t = (tid >= 0) ? thash_find((uint32_t)tid) : NULL;
  if (t)
  {
    if (t->state == THREAD_BLOCKED)
    {
      if (t->wake_tick)
        sleep_remove(t);
      make_runnable(t, gdt_current_cpu_id(), local);
    }
    else if (t->state == THREAD_RUNNING || t->state == THREAD_READY)
    {
      t->wake_pending = true;
    }
  }
  unlock_and_kick(local);
  irq_restore(flags);
}
void scheduler_exit(void)
{
  (void)spin_lock_irqsave(&runqueue_lock);
  struct thread *t = local_scheduler()->current;
  if (t->idle)
    kernel_panic("sched: idle cannot exit");
  account_current(local_scheduler(), timer_ticks());
  t->state = THREAD_TERMINATED;
  schedule_locked();
  kernel_panic("sched: terminated thread resumed");
}

void scheduler_on_timer_interrupt(void)
{
  struct scheduler_cpu *cpu = local_scheduler();
  if (!cpu->started)
    return;
  uint64_t flags = spin_lock_irqsave(&runqueue_lock);
  uint32_t id = gdt_current_cpu_id();
  uint64_t now = timer_ticks();
  account_current(cpu, now);
  wake_expired_sleepers(cpu, id);
  if (now >= cpu->next_balance)
  {
    cpu->next_balance = now + SCHEDULER_BALANCE_INTERVAL_MS;
    (void)pull_task(id, false);
  }
  struct thread *current = cpu->current;
  if ((current->ticks_remaining == 0 || current->idle) &&
      cpu->preempt_depth)
  {
    cpu->reschedule_pending = true;
    unlock_and_kick(cpu);
  }
  else if (current->ticks_remaining == 0 || current->idle)
  {
    cpu->reschedule_pending = false;
    schedule_locked();
  }
  else
  {
    unlock_and_kick(cpu);
  }
  irq_restore(flags);
}
void scheduler_on_reschedule_interrupt(void)
{
  if (!scheduler_is_running())
    return;
  struct scheduler_cpu *cpu = local_scheduler();
  if (cpu->preempt_depth)
  {
    cpu->reschedule_pending = true;
    return;
  }
  scheduler_yield();
}
void scheduler_start(void)
{
  uint64_t flags = spin_lock_irqsave(&runqueue_lock);
  struct scheduler_cpu *cpu = local_scheduler();
  cpu->started = true;
  cpu->last_tick = timer_ticks();
  __atomic_fetch_or(&active_mask, 1U << gdt_current_cpu_id(), __ATOMIC_RELEASE);
  spin_unlock_irqrestore(&runqueue_lock, flags);
  if (gdt_current_cpu_id() == 0)
  {
    if (lapic_start_timer())
      idt_mask_irq(0);
    smp_release_scheduler();
  }
  scheduler_yield();
}

void scheduler_idle_loop(void)
{
  for (;;)
  {
    __asm__ volatile("cli" ::: "memory");
    scheduler_yield();
    __asm__ volatile("sti; hlt" ::: "memory");
  }
}

void scheduler_enter_kernel(void)
{
  struct thread *t = scheduler_current_thread();
  if (!t || !t->user_mode)
    return;
  uint64_t flags = spin_lock_irqsave(&runqueue_lock);
  t->kernel_only = true;
  spin_unlock_irqrestore(&runqueue_lock, flags);
  if (gdt_current_cpu_id() != 0)
  {
    smp_reschedule_cpu(0);
    scheduler_yield();
  }
}
void scheduler_leave_kernel(void)
{
  struct thread *t = scheduler_current_thread();
  if (!t || !t->user_mode)
    return;
  uint64_t flags = spin_lock_irqsave(&runqueue_lock);
  t->kernel_only = false;
  uint32_t here = gdt_current_cpu_id();
  if (t->affinity == -1 || t->affinity_auto)
  {
    uint32_t target = here;
    uint64_t target_load = cpu_load(here);
    for (uint32_t id = 0; id < cpu_registered_count() && id < CPU_MAX_COUNT;
         id++)
    {
      if (id == here || !cpu_schedulers[id].started)
        continue;
      uint64_t load = cpu_load(id);
      if (load < target_load)
      {
        target = id;
        target_load = load;
      }
    }
    if (target != here)
    {
      t->affinity = (int16_t)target;
      t->affinity_auto = true;
    }
    else
    {
      t->affinity = -1;
      t->affinity_auto = false;
    }
  }
  bool must_move = !eligible(t, here);
  spin_unlock_irqrestore(&runqueue_lock, flags);
  if (must_move)
  {
    smp_reschedule_all();
    scheduler_yield();
  }
}
