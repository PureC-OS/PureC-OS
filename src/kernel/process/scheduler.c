#include "scheduler.h"
#include "../../drivers/interrupts/timer.h"
#include "../diagnostics/klog.h"
#include "../diagnostics/panic.h"
#include "../../arch/x86_64/gdt/include/gdt.h"
#include "../../mm/vmm.h"
#include "../../lib/string.h"

static struct thread threads[SCHEDULER_MAX_THREADS];
static struct thread *current = NULL;
static uint32_t next_id = 1;
static bool initialized = false;
static bool started = false;
static volatile bool need_resched = false;
static uint32_t core_count = 1;
static volatile uint64_t total_ticks;
static volatile uint64_t idle_ticks;
static void thread_trampoline(void);
static struct thread *pick_next(void);
static uint64_t create_initial_stack(struct thread *thread);
static int create_thread(void (*entry)(void *arg), void *arg, const char *name,
                         uint8_t priority, int16_t affinity,
                         uint64_t address_space, struct process *process,
                         bool user_mode);

extern void scheduler_asm_switch(uint64_t *old_rsp, uint64_t *new_rsp);

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
    // Idle runs on the boot stack before the first scheduler_start() switch
    // and keeps that saved boot-stack RSP afterwards (0xffffffff80xxxxxx).
    // That address is outside threads[0].stack but is a legitimate kernel
    // stack, so allow it for id==0 only. All other threads must stay inside
    // their own stack.
    if(thread->id==0) return true;
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

static void idle_thread_func(void *arg){
    (void)arg;
    for(;;){
        __asm__ volatile("hlt");
    }
}

static void thread_trampoline(void){
    // This runs as new thread's first execution after ret.
    struct thread *self = current;
    if(self && self->entry){
        klogf(KLOG_DEBUG, "sched: thread %u (%s) started on core %u", self->id, self->name, 0);
        /* User threads enter ring 3 via arch_enter_user(), which starts with
           cli and does not return. Enabling interrupts here races the timer
           against the first context switch and can corrupt saved RSP values. */
        if(!self->user_mode) __asm__ volatile("sti":::"memory");
        self->entry(self->arg);
    }
    if(self && self->id == 0){
        for(;;) __asm__ volatile("hlt");
    }
    scheduler_exit();
    for(;;) __asm__ volatile("hlt");
}

void scheduler_init(void){
    if(initialized) return;
    memset(threads, 0, sizeof(threads));
    /* The Limine response describes detected CPUs, not CPUs currently running
       this scheduler. AP startup and per-CPU scheduler state are not installed
       yet, so advertising those CPUs makes affinity silently target cores that
       never execute kernel threads. */
    core_count = 1;
    struct thread *idle = &threads[0];
    idle->id = 0;
    idle->state = THREAD_RUNNING;
    idle->priority = 7;
    idle->affinity = -1;
    idle->ticks_remaining = SCHEDULER_TIME_SLICE_MS;
    strncpy(idle->name, "idle", sizeof(idle->name)-1);
    idle->entry = idle_thread_func;
    idle->address_space=vmm_kernel_address_space();
    idle->rsp=create_initial_stack(idle);
    current = idle;
    initialized = true;
    klogf(KLOG_OK, "sched: initialized, cores=%u max_threads=%u stack=%u", core_count, SCHEDULER_MAX_THREADS, SCHEDULER_STACK_SIZE);
}

static struct thread *alloc_thread(void){
    for(int i=1;i<SCHEDULER_MAX_THREADS;i++){
        if(threads[i].state==THREAD_FREE
           || threads[i].state==THREAD_TERMINATED){
            return &threads[i];
        }
    }
    return NULL;
}

static uint64_t create_initial_stack(struct thread *thread){
    uint64_t stack_top=(uint64_t)(thread->stack+SCHEDULER_STACK_SIZE);
    stack_top&=~0xFULL;
    uint64_t *stack_ptr=(uint64_t*)stack_top;

    /* scheduler_asm_switch restores six callee-saved registers and returns.
       Keep a dummy return slot above the trampoline so its entry RSP is 8
       modulo 16, exactly as required by the x86_64 System V ABI. */
    *--stack_ptr=0;
    *--stack_ptr=(uint64_t)thread_trampoline;
    for(int register_index=0;register_index<6;register_index++) *--stack_ptr=0;
    return (uint64_t)stack_ptr;
}

static int create_thread(void (*entry)(void *arg), void *arg, const char *name,
                         uint8_t priority, int16_t affinity,
                         uint64_t address_space, struct process *process,
                         bool user_mode){
    if(!initialized) return -1;
    if(!entry) return -1;
    if(priority>7) priority=7;
    struct thread *t = alloc_thread();
    if(!t) return -1;
    memset(t, 0, sizeof(*t));
    t->id = next_id++;
    t->entry = entry;
    t->arg = arg;
    t->state = THREAD_READY;
    t->priority = priority;
    t->affinity = affinity;
    t->ticks_remaining = SCHEDULER_TIME_SLICE_MS;
    t->address_space=address_space ? address_space : vmm_kernel_address_space();
    t->process=process;
    t->user_mode=user_mode;
    if(name) strncpy(t->name, name, sizeof(t->name)-1);
    else strncpy(t->name, "thread", sizeof(t->name)-1);

    t->rsp=create_initial_stack(t);

    if(affinity>=0 && (uint32_t)affinity>=core_count){
        klogf(KLOG_WARN, "sched: thread %u affinity %d exceeds core count %u, using any", t->id, affinity, core_count);
        t->affinity = -1;
    }

    klogf(KLOG_INFO, "sched: created thread %u (%s) prio=%u affinity=%d entry=%p arg=%p stack=%p rsp=0x%llx",
          t->id, t->name, t->priority, t->affinity, entry, arg, t->stack, t->rsp);
    return t->id;
}

int scheduler_create_thread(void (*entry)(void *arg), void *arg,
                            const char *name, uint8_t priority,
                            int16_t affinity){
    return create_thread(entry,arg,name,priority,affinity,
                         vmm_kernel_address_space(),0,false);
}

int scheduler_create_user_thread(void (*entry)(void *arg), void *arg,
                                 const char *name, uint8_t priority,
                                 int16_t affinity, uint64_t address_space,
                                 struct process *process){
    if(!address_space || !process) return -1;
    return create_thread(entry,arg,name,priority,affinity,address_space,
                         process,true);
}

struct thread *scheduler_current_thread(void){ return current; }
int scheduler_current_tid(void){ return current ? (int)current->id : -1; }
uint32_t scheduler_thread_count(void){
    uint32_t cnt=0;
    for(int i=0;i<SCHEDULER_MAX_THREADS;i++){
        if(threads[i].state!=THREAD_FREE
           && threads[i].state!=THREAD_TERMINATED) cnt++;
    }
    return cnt;
}
uint64_t scheduler_thread_runtime_ticks(int tid){
    for(int i=0;i<SCHEDULER_MAX_THREADS;i++)
        if(threads[i].id==(uint32_t)tid
           && threads[i].state!=THREAD_FREE) return threads[i].runtime_ticks;
    return 0;
}
uint64_t scheduler_total_ticks(void){ return total_ticks; }
uint64_t scheduler_idle_ticks(void){ return idle_ticks; }
void scheduler_set_affinity(int tid, int16_t core){
    for(int i=0;i<SCHEDULER_MAX_THREADS;i++) if(threads[i].id==(uint32_t)tid){
        if(core>=0 && (uint32_t)core>=core_count) return;
        threads[i].affinity=core;
        klogf(KLOG_INFO, "sched: thread %u affinity -> %d", tid, core);
        return;
    }
}
int scheduler_get_core_count(void){ return (int)core_count; }

static void wake_sleeping_threads(void){
    uint64_t now=timer_ticks();
    for(int i=0;i<SCHEDULER_MAX_THREADS;i++){
        if(threads[i].state==THREAD_BLOCKED && threads[i].wake_tick!=0
           && now>=threads[i].wake_tick){
            threads[i].state=THREAD_READY;
            threads[i].wake_tick=0;
        }
    }
}

static struct thread *pick_next(void){
    if(!current) return NULL;
    wake_sleeping_threads();
    int start = -1;
    for(int i=0;i<SCHEDULER_MAX_THREADS;i++) if(&threads[i]==current) { start=i; break; }
    if(start<0) start=0;
    /* Strict priority selection starved lower-priority work whenever a
       CPU-bound higher-priority task stayed runnable. */
    for(int iter=1; iter<SCHEDULER_MAX_THREADS; iter++){
        int idx = (start+iter)%SCHEDULER_MAX_THREADS;
        if(idx != 0 && threads[idx].state==THREAD_READY) return &threads[idx];
    }
    if(current && current->id != 0 && current->state==THREAD_RUNNING) return current;
    threads[0].state = THREAD_RUNNING;
    return &threads[0];
    return NULL;
}

static void activate_thread(struct thread *thread){
    gdt_set_kernel_stack((uint64_t)(uintptr_t)
                         (thread->stack+SCHEDULER_STACK_SIZE));
    vmm_switch_address_space(thread->address_space);
}

void scheduler_yield(void){
    if(!initialized) return;
    uint64_t flags;
    __asm__ volatile("pushfq; pop %0; cli":"=r"(flags)::"memory");
    struct thread *prev = current;
    struct thread *next = pick_next();
    if(!next || next==prev){
        if(flags & (1ULL<<9)) __asm__ volatile("sti":::"memory");
        return;
    }
    if(prev->state==THREAD_RUNNING) prev->state=THREAD_READY;
    next->state=THREAD_RUNNING;
    current=next;
    prev->ticks_remaining = SCHEDULER_TIME_SLICE_MS;
    next->ticks_remaining = SCHEDULER_TIME_SLICE_MS;
    activate_thread(next);
    validate_switch_target(prev, next);
    // klogf(KLOG_DEBUG, "sched: yield %u (%s) -> %u (%s)", prev->id, prev->name, next->id, next->name);
    scheduler_asm_switch(&prev->rsp, &next->rsp);
    if(flags & (1ULL<<9)) __asm__ volatile("sti":::"memory");
}

void scheduler_sleep(uint32_t milliseconds){
    if(!initialized || !current || milliseconds==0){
        scheduler_yield();
        return;
    }
    uint64_t now=timer_ticks();
    uint64_t wake=now+milliseconds;
    current->wake_tick=wake<now ? UINT64_MAX : wake;
    scheduler_block();
}

void scheduler_block(void){
    if(!current) return;
    uint64_t flags;
    __asm__ volatile("pushfq; pop %0; cli":"=r"(flags)::"memory");
    current->state = THREAD_BLOCKED;
    struct thread *prev = current;
    struct thread *next = pick_next();
    if(!next){
        klog(KLOG_ERROR, "sched: no thread to schedule after block!");
        for(;;) __asm__ volatile("cli; hlt");
    }
    if(next==prev){
        // Only runnable thread blocks itself (typical before
        // scheduler_start, when idle runs on the boot stack).
        // A self-switch would overwrite the thread's saved RSP with the
        // current one; just stay running instead of deadlocking.
        prev->state = THREAD_RUNNING;
        if(flags & (1ULL<<9)) __asm__ volatile("sti":::"memory");
        return;
    }
    next->state = THREAD_RUNNING;
    current = next;
    activate_thread(next);
    validate_switch_target(prev, next);
    scheduler_asm_switch(&prev->rsp, &next->rsp);
    if(flags & (1ULL<<9)) __asm__ volatile("sti":::"memory");
}

void scheduler_unblock(int tid){
    uint64_t flags;
    __asm__ volatile("pushfq; pop %0; cli":"=r"(flags)::"memory");
    for(int i=0;i<SCHEDULER_MAX_THREADS;i++) if(threads[i].id==(uint32_t)tid){
        if(threads[i].state==THREAD_BLOCKED){
            threads[i].state=THREAD_READY;
            klogf(KLOG_DEBUG, "sched: thread %u unblocked", tid);
        }
        break;
    }
    if(flags & (1ULL<<9)) __asm__ volatile("sti":::"memory");
}

void scheduler_exit(void){
    if(!current) for(;;) __asm__ volatile("cli; hlt");
    uint64_t flags;
    __asm__ volatile("pushfq; pop %0; cli":"=r"(flags)::"memory");
    klogf(KLOG_INFO, "sched: thread %u (%s) exiting", current->id, current->name);
    current->state = THREAD_TERMINATED;
    struct thread *prev = current;
    struct thread *next = pick_next();
    if(!next || next==prev){
        klog(KLOG_WARN, "sched: last thread exiting, halting");
        for(;;) __asm__ volatile("cli; hlt");
    }
    next->state = THREAD_RUNNING;
    current = next;
    activate_thread(next);
    validate_switch_target(prev, next);
    scheduler_asm_switch(&prev->rsp, &next->rsp);
    if(flags & (1ULL<<9)) __asm__ volatile("sti":::"memory");
    for(;;) __asm__ volatile("hlt");
}

static void scheduler_tick(void){
    if(!initialized || !current) return;
    total_ticks++;
    current->runtime_ticks++;
    if(current->id==0) idle_ticks++;
    if(current->ticks_remaining>0) current->ticks_remaining--;
    if(current->ticks_remaining==0){
        need_resched = true;
    }
    wake_sleeping_threads();
}

static void scheduler_schedule(void){
    if(!need_resched) return;
    need_resched=false;
    scheduler_yield();
}

void scheduler_on_timer_interrupt(void){
    if(!started) return;
    scheduler_tick();
    scheduler_schedule();
}

void scheduler_start(void){
    if(!initialized) for(;;) __asm__ volatile("cli; hlt");
    klogf(KLOG_INFO, "sched: starting with %u threads", scheduler_thread_count());
    started=true;
    scheduler_yield();
}
