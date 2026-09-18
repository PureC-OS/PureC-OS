#include "scheduler.h"
#include "../smp/cpu.h"
#include "../smp/smp.h"
#include "../sync/spinlock.h"
#include "../../drivers/interrupts/timer.h"
#include "../diagnostics/klog.h"
#include "../diagnostics/panic.h"
#include "../../arch/x86_64/gdt/include/gdt.h"
#include "../../arch/x86_64/fpu/include/fpu.h"
#include "../../mm/vmm.h"
#include "../../mm/pmm.h"
#include "../../lib/string.h"

_Static_assert(sizeof(struct thread)<=4096,
    "thread node must fit in a single PMM page");

static struct thread *thread_list;
static uint32_t thread_live_count;
static struct thread idle_threads[CPU_MAX_COUNT];
struct scheduler_cpu {
    struct thread *current;
    struct thread *idle;
    struct thread *previous;
    bool initialized;
    bool started;
    uint32_t cursor;
    uint64_t last_tick;
    uint64_t total_ticks;
    uint64_t idle_ticks;
    uint32_t preempt_depth;
    bool reschedule_pending;
} __attribute__((aligned(64)));
static struct scheduler_cpu cpu_schedulers[CPU_MAX_COUNT];
static spinlock_t runqueue_lock = SPINLOCK_INIT;
static uint32_t next_id = 1;
static uint32_t active_mask;

static struct scheduler_cpu *local_scheduler(void){
    uint32_t id = gdt_current_cpu_id();
    if(id >= CPU_MAX_COUNT) kernel_panic("sched: CPU has no kernel GDT");
    return &cpu_schedulers[id];
}
static void thread_trampoline(void);
static uint64_t create_initial_stack(struct thread *thread);
static bool kernel_text_address_valid(uint64_t address){
    // Kernel linked at 0xffffffff80000000 (higher half).
    // Trampoline / thread entry must live there; 0x8000 etc. = corruption.
    return address >= 0xffffffff80000000ULL;
}

static bool thread_stack_valid(const struct thread *thread){
    if(!thread) return false;
    if(thread->rsp & 0x7ULL) return false;
    // Low addresses (0x0, 0x8000, NULL) are always corruption.
    if(thread->rsp < 0xffff800000000000ULL) return false;
    uint64_t stack_base=(uint64_t)(uintptr_t)thread->stack;
    uint64_t stack_top=stack_base+SCHEDULER_STACK_SIZE;
    if(thread->rsp >= stack_base+64 && thread->rsp < stack_top) return true;
    if(thread->idle) return true;
    return false;
}

static bool thread_stack_return_valid(uint64_t rsp){
    if(!rsp) return false;
    // RSP must be readable kernel memory; probe via canonical high-half check
    // done by caller with thread_stack_valid(). Here only check return slot.
    uint64_t *slot=(uint64_t*)rsp;
    uint64_t return_address=slot[6];
    // Zero = never initialized; low address like 0x8000 = stack corruption
    // (e.g. interrupt frame popped from wrong stack after bad switch).
    if(!kernel_text_address_valid(return_address)) return false;
    return true;
}

static void write_hex_digits(char *out, uint64_t value, int digits){
    static const char hexdigits[]="0123456789abcdef";
    for(int i=digits-1;i>=0;i--){
        out[digits-1-i]=hexdigits[(value >> (i*4)) & 0xF];
    }
}

// panic_begin() clears the screen, so klogf() before kernel_panic() is
// wiped. Format all target details INTO the reason string instead.
/* Called with the runqueue lock held. */
static char sched_panic_reason[224];

static void validate_switch_target(const struct thread *prev,
                                     const struct thread *next){
    if(!next){
        kernel_panic("scheduler: null next thread");
    }
    if(!thread_stack_valid(next)){
        // "sched bad RSP tgt=<id> rsp=<rsp> base=<base> prev=<id>"
        char *p=sched_panic_reason;
        const char *prefix="sched: bad RSP tgt=0x";
        for(int i=0;prefix[i];i++) *p++=prefix[i];
        write_hex_digits(p, next->id, 8); p+=8;
        const char *mid=" rsp=0x"; for(int i=0;mid[i];i++) *p++=mid[i];
        write_hex_digits(p, next->rsp, 16); p+=16;
        const char *mid2=" base=0x"; for(int i=0;mid2[i];i++) *p++=mid2[i];
        write_hex_digits(p, (uint64_t)(uintptr_t)next->stack, 16); p+=16;
        const char *mid3=" prev=0x"; for(int i=0;mid3[i];i++) *p++=mid3[i];
        write_hex_digits(p, prev ? prev->id : 0xFFFFFFFFu, 8); p+=8;
        *p='\0';
        kernel_panic(sched_panic_reason);
    }
    if(!thread_stack_return_valid(next->rsp)){
        uint64_t *slot=(uint64_t*)next->rsp;
        uint64_t ret=slot[6];
        char *p=sched_panic_reason;
        const char *prefix="sched: bad return 0x";
        for(int i=0;prefix[i];i++) *p++=prefix[i];
        write_hex_digits(p, ret, 16); p+=16;
        const char *mid=" tgt=0x"; for(int i=0;mid[i];i++) *p++=mid[i];
        write_hex_digits(p, next->id, 8); p+=8;
        const char *mid2=" prev=0x"; for(int i=0;mid2[i];i++) *p++=mid2[i];
        write_hex_digits(p, prev ? prev->id : 0xFFFFFFFFu, 8); p+=8;
        *p='\0';
        kernel_panic(sched_panic_reason);
    }
}


static void finish_switch(void){
    struct scheduler_cpu *cpu = local_scheduler();
    struct thread *previous = cpu->previous;
    if(previous) previous->running_cpu = -1;
    cpu->previous = NULL;
    spin_unlock(&runqueue_lock);
}

static void thread_trampoline(void){
    finish_switch();
    struct thread *self = scheduler_current_thread();
    if(!self->user_mode) __asm__ volatile("sti" ::: "memory");
    self->entry(self->arg);
    scheduler_exit();
    __builtin_unreachable();
}

static uint64_t create_initial_stack(struct thread *thread){
    uint64_t *sp = (uint64_t*)(thread->stack + SCHEDULER_STACK_SIZE);
    *--sp = 0;
    *--sp = (uint64_t)thread_trampoline;
    for(unsigned i=0; i<6; i++) *--sp = 0;
    return (uint64_t)sp;
}

void scheduler_init_cpu(void){
    uint32_t id = gdt_current_cpu_id();
    struct scheduler_cpu *cpu = local_scheduler();
    if(cpu->initialized) return;
    struct thread *idle = &idle_threads[id];
    memset(idle, 0, sizeof(*idle));
    idle->idle = true;
    idle->state = THREAD_RUNNING;
    idle->running_cpu = (int16_t)id;
    idle->affinity = (int16_t)id;
    idle->address_space = vmm_kernel_address_space();
    idle->ticks_remaining = SCHEDULER_TIME_SLICE_MS;
    strncpy(idle->name, "idle", sizeof(idle->name)-1);
    fpu_thread_init(idle->fpu_state);
    cpu->idle = idle;
    cpu->current = idle;
    cpu->cursor = id % SCHEDULER_MAX_THREADS;
    cpu->last_tick = timer_ticks();
    cpu->initialized = true;
}

void scheduler_init(void){
    if(gdt_current_cpu_id() != 0 || local_scheduler()->initialized) return;
    memset(threads, 0, sizeof(threads));
    for(unsigned i=0; i<SCHEDULER_MAX_THREADS; i++) threads[i].running_cpu = -1;
    scheduler_init_cpu();
    klogf(KLOG_OK, "sched: shared locked runqueue, max_threads=%u", SCHEDULER_MAX_THREADS);
}

static struct thread *alloc_thread(void){
    for(unsigned i=0; i<SCHEDULER_MAX_THREADS; i++){
        if((threads[i].state==THREAD_FREE || threads[i].state==THREAD_TERMINATED)
           && threads[i].running_cpu == -1) return &threads[i];
    }
    return NULL;
}

static int create_thread(void (*entry)(void*), void *arg, const char *name,
                         uint8_t priority, int16_t affinity, uint64_t address_space,
                         struct process *process, bool user_mode){
    if(!local_scheduler()->initialized || !entry) return -1;
    if(affinity < -1 || affinity >= (int16_t)cpu_registered_count()) return -1;
    if(affinity >= 0 && !cpu_is_online((uint32_t)affinity)) return -1;
    uint64_t flags = spin_lock_irqsave(&runqueue_lock);
    struct thread *t = alloc_thread();
    if(!t){ spin_unlock_irqrestore(&runqueue_lock, flags); return -1; }
    memset(t, 0, sizeof(*t));
    t->id = next_id++;
    t->entry = entry;
    t->arg = arg;
    t->priority = priority > 7 ? 7 : priority;
    t->affinity = affinity;
    t->running_cpu = -1;
    t->kernel_only = user_mode; /* User entry setup is a BSP kernel service. */
    t->ticks_remaining = SCHEDULER_TIME_SLICE_MS;
    t->address_space = address_space ? address_space : vmm_kernel_address_space();
    t->process = process;
    t->user_mode = user_mode;
    strncpy(t->name, name ? name : "thread", sizeof(t->name)-1);
    t->rsp = create_initial_stack(t);
    fpu_thread_init(t->fpu_state);
    t->state = THREAD_READY; /* Publish only after the complete context exists. */
    int id = (int)t->id;
    spin_unlock_irqrestore(&runqueue_lock, flags);
    smp_reschedule_all();
    return id;
}

int scheduler_create_thread(void (*entry)(void*), void *arg, const char *name,
                            uint8_t priority, int16_t affinity){
    return create_thread(entry,arg,name,priority,affinity,vmm_kernel_address_space(),NULL,false);
}
int scheduler_create_user_thread(void (*entry)(void*), void *arg, const char *name,
                                 uint8_t priority, int16_t affinity,
                                 uint64_t address_space, struct process *process){
    if(!address_space || !process) return -1;
    return create_thread(entry,arg,name,priority,affinity,address_space,process,true);
}

struct thread *scheduler_current_thread(void){
    uint32_t id = gdt_current_cpu_id();
    return id < CPU_MAX_COUNT ? cpu_schedulers[id].current : NULL;
}
int scheduler_current_tid(void){
    struct thread *t = scheduler_current_thread();
    return t ? (int)t->id : -1;
}
uint32_t scheduler_active_mask(void){ return __atomic_load_n(&active_mask, __ATOMIC_ACQUIRE); }
bool scheduler_is_running(void){
    uint32_t id = gdt_current_cpu_id();
    return id < CPU_MAX_COUNT && cpu_schedulers[id].started;
}
int scheduler_get_core_count(void){
    uint32_t mask=scheduler_active_mask();
    int count=0;
    while(mask){ count+=(int)(mask&1); mask>>=1; }
    return count;
}

uint32_t scheduler_thread_count(void){
    uint64_t flags = spin_lock_irqsave(&runqueue_lock);
    uint32_t count = 0;
    for(unsigned i=0; i<SCHEDULER_MAX_THREADS; i++)
        if(threads[i].state!=THREAD_FREE && threads[i].state!=THREAD_TERMINATED) count++;
    spin_unlock_irqrestore(&runqueue_lock, flags);
    return count;
}
uint64_t scheduler_thread_runtime_ticks(int tid){
    uint64_t flags = spin_lock_irqsave(&runqueue_lock);
    uint64_t result = 0;
    for(unsigned i=0; i<SCHEDULER_MAX_THREADS; i++)
        if(threads[i].id==(uint32_t)tid && threads[i].state!=THREAD_FREE){ result=threads[i].runtime_ticks; break; }
    spin_unlock_irqrestore(&runqueue_lock, flags);
    return result;
}
bool scheduler_thread_stopped(int tid){
    uint64_t flags = spin_lock_irqsave(&runqueue_lock);
    bool stopped = true;
    for(unsigned i=0; i<SCHEDULER_MAX_THREADS; i++)
        if(threads[i].id==(uint32_t)tid){
            stopped = threads[i].state==THREAD_TERMINATED && threads[i].running_cpu==-1;
            break;
        }
    spin_unlock_irqrestore(&runqueue_lock, flags);
    return stopped;
}
/* Aggregate CPU milliseconds; process percentages use a wall-clock denominator. */
uint64_t scheduler_total_ticks(void){
    uint64_t total=0;
    for(unsigned i=0;i<CPU_MAX_COUNT;i++) total+=__atomic_load_n(&cpu_schedulers[i].total_ticks,__ATOMIC_RELAXED);
    return total;
}
uint64_t scheduler_idle_ticks(void){
    uint64_t total=0;
    for(unsigned i=0;i<CPU_MAX_COUNT;i++) total+=__atomic_load_n(&cpu_schedulers[i].idle_ticks,__ATOMIC_RELAXED);
    return total;
}
bool scheduler_cpu_ticks(uint32_t id, uint64_t *total, uint64_t *idle){
    if(id>=CPU_MAX_COUNT) return false;
    if(total) *total=__atomic_load_n(&cpu_schedulers[id].total_ticks,__ATOMIC_RELAXED);
    if(idle) *idle=__atomic_load_n(&cpu_schedulers[id].idle_ticks,__ATOMIC_RELAXED);
    return true;
}

void scheduler_set_affinity(int tid, int16_t core){
    if(core < -1 || core >= (int16_t)cpu_registered_count()) return;
    if(core >= 0 && !cpu_is_online((uint32_t)core)) return;
    uint64_t flags = spin_lock_irqsave(&runqueue_lock);
    for(unsigned i=0;i<SCHEDULER_MAX_THREADS;i++)
        if(threads[i].id==(uint32_t)tid && threads[i].state!=THREAD_FREE){ threads[i].affinity=core; break; }
    spin_unlock_irqrestore(&runqueue_lock, flags);
    smp_reschedule_all();
}

static bool eligible(const struct thread *t, uint32_t id){
    /* Kernel-only is a temporary service placement constraint. The original
       userspace affinity is preserved and restored on return to ring 3. */
    if(t->kernel_only) return id == 0;
    return t->affinity < 0 || t->affinity == (int16_t)id;
}
static void wake_sleeping_threads(void){
    uint64_t now=timer_ticks();
    for(unsigned i=0;i<SCHEDULER_MAX_THREADS;i++){
        struct thread *t=&threads[i];
        if(t->state==THREAD_BLOCKED && t->wake_tick && now>=t->wake_tick){
            t->state=THREAD_READY;
            t->wake_tick=0;
        }
    }
}
static struct thread *pick_next(void){
    uint32_t id = gdt_current_cpu_id();
    struct scheduler_cpu *cpu = local_scheduler();
    wake_sleeping_threads();
    for(unsigned n=1; n<=SCHEDULER_MAX_THREADS; n++){
        unsigned index=(cpu->cursor+n)%SCHEDULER_MAX_THREADS;
        struct thread *t=&threads[index];
        if(t->state==THREAD_READY && t->running_cpu==-1 && eligible(t,id)){
            cpu->cursor=index;
            return t;
        }
    }
    if(!cpu->current->idle && cpu->current->state==THREAD_RUNNING && eligible(cpu->current,id))
        return cpu->current;
    return cpu->idle;
}

/* Enter with runqueue_lock held and interrupts disabled; always return with
   the lock released, potentially on another CPU. */
static void schedule_locked(void){
    struct scheduler_cpu *cpu=local_scheduler();
    struct thread *prev=cpu->current;
    struct thread *next=pick_next();
    if(next==prev){
        prev->state=THREAD_RUNNING;
        prev->ticks_remaining=SCHEDULER_TIME_SLICE_MS;
        spin_unlock(&runqueue_lock);
        return;
    }
    if(prev->state==THREAD_RUNNING) prev->state=THREAD_READY;
    next->state=THREAD_RUNNING;
    uint32_t id=gdt_current_cpu_id();
    if(next->cpu_mask && !(next->cpu_mask & (1U<<id))) next->migrations++;
    next->cpu_mask |= 1U<<id;
    next->running_cpu=(int16_t)id;
    next->ticks_remaining=SCHEDULER_TIME_SLICE_MS;
    cpu->previous=prev;
    cpu->current=next;
    validate_switch_target(prev,next);
    fpu_save(prev->fpu_state);
    gdt_set_kernel_stack((uint64_t)(uintptr_t)(next->stack+SCHEDULER_STACK_SIZE));
    vmm_switch_address_space(next->address_space);
    fpu_restore(next->fpu_state);
    scheduler_asm_switch(&prev->rsp,&next->rsp);
    finish_switch();
}

void scheduler_yield(void){
    if(!local_scheduler()->initialized) return;
    struct scheduler_cpu *cpu=local_scheduler();
    if(cpu->preempt_depth){
        cpu->reschedule_pending=true;
        return;
    }
    uint64_t flags=spin_lock_irqsave(&runqueue_lock);
    schedule_locked();
    irq_restore(flags);
}
bool scheduler_preempt_disable(void){
    uint32_t id=gdt_current_cpu_id();
    if(id>=CPU_MAX_COUNT) return false;
    struct scheduler_cpu *cpu=&cpu_schedulers[id];
    if(!cpu->initialized || !cpu->started) return false;
    uint64_t flags=irq_save();
    cpu->preempt_depth++;
    irq_restore(flags);
    return true;
}
void scheduler_preempt_enable(void){
    struct scheduler_cpu *cpu=local_scheduler();
    if(!cpu->initialized) return;
    uint64_t flags=irq_save();
    if(!cpu->preempt_depth){
        irq_restore(flags);
        kernel_panic("sched: unbalanced preempt enable");
    }
    cpu->preempt_depth--;
    bool schedule=cpu->preempt_depth==0 && cpu->reschedule_pending
        && cpu->started;
    if(schedule) cpu->reschedule_pending=false;
    irq_restore(flags);
    if(schedule) scheduler_yield();
}
void scheduler_sleep(uint32_t milliseconds){
    if(!milliseconds){ scheduler_yield(); return; }
    if(!local_scheduler()->initialized) return;
    uint64_t flags=spin_lock_irqsave(&runqueue_lock);
    struct thread *t=local_scheduler()->current;
    if(t->idle){ spin_unlock_irqrestore(&runqueue_lock,flags); return; }
    uint64_t now=timer_ticks();
    t->wake_tick=now+milliseconds;
    if(t->wake_tick<now) t->wake_tick=UINT64_MAX;
    t->state=THREAD_BLOCKED;
    t->wake_pending=false;
    schedule_locked();
    irq_restore(flags);
}
void scheduler_block(void){
    if(!local_scheduler()->initialized) return;
    uint64_t flags=spin_lock_irqsave(&runqueue_lock);
    struct thread *t=local_scheduler()->current;
    if(t->idle || t->wake_pending){
        t->wake_pending=false;
        spin_unlock_irqrestore(&runqueue_lock,flags);
        return;
    }
    t->state=THREAD_BLOCKED;
    schedule_locked();
    irq_restore(flags);
}
void scheduler_unblock(int tid){
    uint64_t flags=spin_lock_irqsave(&runqueue_lock);
    for(unsigned i=0;i<SCHEDULER_MAX_THREADS;i++){
        struct thread *t=&threads[i];
        if(t->id!=(uint32_t)tid) continue;
        if(t->state==THREAD_BLOCKED){ t->wake_tick=0; t->state=THREAD_READY; }
        else if(t->state==THREAD_RUNNING || t->state==THREAD_READY) t->wake_pending=true;
        break;
    }
    spin_unlock_irqrestore(&runqueue_lock,flags);
    smp_reschedule_all();
}
void scheduler_exit(void){
    (void)spin_lock_irqsave(&runqueue_lock);
    struct thread *t=local_scheduler()->current;
    if(t->idle) kernel_panic("sched: idle cannot exit");
    t->state=THREAD_TERMINATED;
    schedule_locked();
    kernel_panic("sched: terminated thread resumed");
}

void scheduler_on_timer_interrupt(void){
    struct scheduler_cpu *cpu=local_scheduler();
    if(!cpu->started) return;
    uint64_t flags=spin_lock_irqsave(&runqueue_lock);
    uint64_t now=timer_ticks();
    uint64_t elapsed=now-cpu->last_tick;
    cpu->last_tick=now;
    __atomic_fetch_add(&cpu->total_ticks,elapsed,__ATOMIC_RELAXED);
    struct thread *t=cpu->current;
    t->runtime_ticks+=elapsed;
    if(t->idle) __atomic_fetch_add(&cpu->idle_ticks,elapsed,__ATOMIC_RELAXED);
    if((elapsed>=t->ticks_remaining || t->idle) && cpu->preempt_depth){
        cpu->reschedule_pending=true;
        spin_unlock(&runqueue_lock);
    } else if(elapsed>=t->ticks_remaining || t->idle){
        cpu->reschedule_pending=false;
        schedule_locked();
    } else {
        t->ticks_remaining-=(uint32_t)elapsed;
        spin_unlock(&runqueue_lock);
    }
    irq_restore(flags);
}
void scheduler_on_reschedule_interrupt(void){
    if(!scheduler_is_running()) return;
    struct scheduler_cpu *cpu=local_scheduler();
    if(cpu->preempt_depth){
        cpu->reschedule_pending=true;
        return;
    }
    scheduler_yield();
}
void scheduler_start(void){
    uint64_t flags=spin_lock_irqsave(&runqueue_lock);
    struct scheduler_cpu *cpu=local_scheduler();
    cpu->started=true;
    cpu->last_tick=timer_ticks();
    __atomic_fetch_or(&active_mask,1U<<gdt_current_cpu_id(),__ATOMIC_RELEASE);
    spin_unlock_irqrestore(&runqueue_lock,flags);
    if(gdt_current_cpu_id()==0) smp_release_scheduler();
    scheduler_yield();
}

void scheduler_idle_loop(void){
    for(;;){
        /* IPI or timer arriving after selection stays pending through STI;HLT.
           Idle threads do not poll devices or remain in the runnable table. */
        __asm__ volatile("cli" ::: "memory");
        scheduler_yield();
        __asm__ volatile("sti; hlt" ::: "memory");
    }
}

void scheduler_enter_kernel(void){
    struct thread *t=scheduler_current_thread();
    if(!t || !t->user_mode) return;
    uint64_t flags=spin_lock_irqsave(&runqueue_lock);
    t->kernel_only=true;
    spin_unlock_irqrestore(&runqueue_lock,flags);
    if(gdt_current_cpu_id()!=0){
        smp_reschedule_cpu(0);
        scheduler_yield();
    }
}
void scheduler_leave_kernel(void){
    struct thread *t=scheduler_current_thread();
    if(!t || !t->user_mode) return;
    uint64_t flags=spin_lock_irqsave(&runqueue_lock);
    t->kernel_only=false;
    bool must_move=!eligible(t,gdt_current_cpu_id());
    spin_unlock_irqrestore(&runqueue_lock,flags);
    if(must_move){ smp_reschedule_all(); scheduler_yield(); }
}
