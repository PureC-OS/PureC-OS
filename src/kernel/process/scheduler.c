#include "scheduler.h"
#include "../smp/smp.h"
#include "../sync/spinlock.h"
#include "../../drivers/interrupts/timer.h"
#include "../diagnostics/klog.h"
#include "../diagnostics/panic.h"
#include "../../arch/x86_64/gdt/include/gdt.h"
#include "../../arch/x86_64/fpu.h"
#include "../../mm/vmm.h"
#include "../../lib/string.h"

static struct thread threads[SCHEDULER_MAX_THREADS];
static spinlock_t sched_lock;
static uint32_t next_id = 1;
static bool initialized = false;
static volatile bool started = false;
static volatile uint64_t total_ticks;
static volatile uint64_t idle_ticks;
#define SCHED_THREAD_CANARY 0x9E3779B97F4A7C15ULL

static void thread_trampoline(void);

static void thread_set_canary(struct thread *t){
    t->canary_head = SCHED_THREAD_CANARY;
    t->canary_tail = SCHED_THREAD_CANARY;
}

static bool thread_canary_ok(const struct thread *t){
    return t && t->canary_head == SCHED_THREAD_CANARY
        && t->canary_tail == SCHED_THREAD_CANARY;
}
static struct thread *pick_next(struct cpu_local *cpu);
static uint64_t create_initial_stack(struct thread *thread);
static int create_thread(void (*entry)(void *arg), void *arg, const char *name,
                         uint8_t priority, int16_t affinity,
                         uint64_t address_space, struct process *process,
                         bool user_mode);

extern void scheduler_asm_switch(uint64_t *old_rsp, uint64_t *new_rsp);

static bool kernel_text_address_valid(uint64_t address){
    return address >= 0xffffffff80000000ULL;
}

static bool thread_stack_valid(const struct thread *thread){
    if(!thread) return false;
    if(thread->rsp & 0x7ULL) return false;
    if(thread->rsp < 0xffff800000000000ULL) return false;
    uint64_t stack_base=(uint64_t)(uintptr_t)thread->stack;
    uint64_t stack_top=stack_base+SCHEDULER_STACK_SIZE;
    if(thread->rsp >= stack_base+64 && thread->rsp < stack_top) return true;
    return false;
}

static bool thread_stack_return_valid(uint64_t rsp){
    if(!rsp) return false;
    uint64_t *slot=(uint64_t*)rsp;
    uint64_t return_address=slot[6];
    if(!kernel_text_address_valid(return_address)) return false;
    return true;
}

static void write_hex_digits(char *out, uint64_t value, int digits){
    static const char hexdigits[]="0123456789abcdef";
    for(int i=digits-1;i>=0;i--){
        out[digits-1-i]=hexdigits[(value >> (i*4)) & 0xF];
    }
}

static char sched_panic_reason[1024];

static void validate_switch_target(const struct thread *prev,
                                     const struct thread *next){
    if(!next){
        kernel_panic("scheduler: null next thread");
    }
    if((prev && !thread_canary_ok(prev)) || !thread_canary_ok(next)){
        char *p=sched_panic_reason;
        const char *prefix="sched: canary dead prev=";
        for(int i=0;prefix[i];i++) *p++=prefix[i];
        write_hex_digits(p, prev ? prev->id : 0xFFFFFFFFu, 8); p+=8;
        const char *mid=" next="; for(int i=0;mid[i];i++) *p++=mid[i];
        write_hex_digits(p, next ? next->id : 0xFFFFFFFFu, 8); p+=8;
        *p='\0';
        spin_unlock(&sched_lock);
        __asm__ volatile("sti" ::: "memory");
        kernel_panic(sched_panic_reason);
    }
    if(!smp_this_ok()){
        char *p=sched_panic_reason;
        const char *prefix="sched: cpu id broken rdpid=";
        for(int i=0;prefix[i];i++) *p++=prefix[i];
        write_hex_digits(p, smp_index_rdpid(), 8); p+=8;
        const char *mid=" lapic="; for(int i=0;mid[i];i++) *p++=mid[i];
        write_hex_digits(p, smp_index_lapic(), 8); p+=8;
        *p='\0';
        spin_unlock(&sched_lock);
        __asm__ volatile("sti" ::: "memory");
        kernel_panic(sched_panic_reason);
    }
    if(!thread_stack_valid(next)){
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
        const char *mid3=" trsp="; for(int i=0;mid3[i];i++) *p++=mid3[i];
        write_hex_digits(p, next->rsp, 16); p+=16;
        const char *mid4=" tstate="; for(int i=0;mid4[i];i++) *p++=mid4[i];
        write_hex_digits(p, next->state, 2); p+=2;
        const char *mid5=" tlast="; for(int i=0;mid5[i];i++) *p++=mid5[i];
        write_hex_digits(p, (uint32_t)(uint16_t)next->last_cpu, 4); p+=4;
        for(uint32_t ci = 0; ci < SMP_MAX_CPUS; ci++){
            struct cpu_local *c = smp_cpu(ci);
            if(!c || !c->present) continue;
            const char *midc=" c";
            for(int i=0;midc[i];i++) *p++=midc[i];
            write_hex_digits(p, ci, 2); p+=2;
            const char *mide="=";
            for(int i=0;mide[i];i++) *p++=mide[i];
            write_hex_digits(p, c->current ? c->current->id : 0xFFFFFFFFu, 8);
            p+=8;
        }
        *p='\0';
        kernel_panic(sched_panic_reason);
    }
}

static void schedule_locked(struct cpu_local *cpu, int prev_state,
                            uint64_t flags, bool restore_flags);

static void scheduler_idle_entry(void *arg){
    struct cpu_local *cpu = arg ? (struct cpu_local *)arg : smp_this();
    for(;;){
        uint64_t flags = spin_lock_irqsave(&sched_lock);
        schedule_locked(cpu, THREAD_READY, flags, true);
    }
}

static void thread_trampoline(void){
    struct thread *self = smp_this()->current;
    if(self && self->entry){
        klogf(KLOG_DEBUG, "sched: thread %u (%s) started on core %u", self->id, self->name, smp_cpu_index());
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
    smp_early_bsp();
    struct thread *idle = &threads[0];
    idle->id = 0;
    thread_set_canary(idle);
    idle->state = THREAD_RUNNING;
    idle->priority = 7;
    idle->affinity = 0;
    idle->ticks_remaining = SCHEDULER_TIME_SLICE_MS;
    strncpy(idle->name, "idle0", sizeof(idle->name)-1);
    idle->entry = scheduler_idle_entry;
    idle->arg = smp_cpu(0);
    idle->address_space=vmm_kernel_address_space();
    idle->rsp=create_initial_stack(idle);
    fpu_thread_init(idle->fpu_state);
    smp_cpu(0)->current = idle;
    smp_cpu(0)->idle = idle;
    initialized = true;
    klogf(KLOG_OK, "sched: initialized, max_threads=%u stack=%u", SCHEDULER_MAX_THREADS, SCHEDULER_STACK_SIZE);
}

static struct thread *alloc_thread_locked(void){
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
    uint64_t flags = spin_lock_irqsave(&sched_lock);
    struct thread *t = alloc_thread_locked();
    if(!t){
        spin_unlock_irqrestore(&sched_lock, flags);
        return -1;
    }
    memset(t, 0, sizeof(*t));
    thread_set_canary(t);
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
    fpu_thread_init(t->fpu_state);

    if(affinity>=0 && (uint32_t)affinity>=smp_online_count()){
        klogf(KLOG_WARN, "sched: thread %u affinity %d exceeds online count %u, using any", t->id, affinity, smp_online_count());
        t->affinity = -1;
    }
    int id = (int)t->id;
    spin_unlock_irqrestore(&sched_lock, flags);

    klogf(KLOG_INFO, "sched: created thread %u (%s) prio=%u affinity=%d entry=%p arg=%p stack=%p rsp=0x%llx",
          t->id, t->name, t->priority, t->affinity, entry, arg, t->stack, t->rsp);
    return id;
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

struct thread *scheduler_current_thread(void){ return smp_this()->current; }
int scheduler_current_tid(void){
    struct thread *t = smp_this()->current;
    return t ? (int)t->id : -1;
}
uint32_t scheduler_thread_count(void){
    uint64_t flags = spin_lock_irqsave(&sched_lock);
    uint32_t cnt=0;
    for(int i=0;i<SCHEDULER_MAX_THREADS;i++){
        if(threads[i].state!=THREAD_FREE
           && threads[i].state!=THREAD_TERMINATED) cnt++;
    }
    spin_unlock_irqrestore(&sched_lock, flags);
    return cnt;
}
uint64_t scheduler_thread_runtime_ticks(int tid){
    uint64_t flags = spin_lock_irqsave(&sched_lock);
    uint64_t runtime = 0;
    for(int i=0;i<SCHEDULER_MAX_THREADS;i++)
        if(threads[i].id==(uint32_t)tid
           && threads[i].state!=THREAD_FREE) runtime=threads[i].runtime_ticks;
    spin_unlock_irqrestore(&sched_lock, flags);
    return runtime;
}
uint64_t scheduler_total_ticks(void){ return total_ticks; }
uint64_t scheduler_idle_ticks(void){ return idle_ticks; }
void scheduler_set_affinity(int tid, int16_t core){
    uint64_t flags = spin_lock_irqsave(&sched_lock);
    for(int i=0;i<SCHEDULER_MAX_THREADS;i++) if(threads[i].id==(uint32_t)tid){
        if(core>=0 && (uint32_t)core>=smp_online_count()) break;
        threads[i].affinity=core;
        klogf(KLOG_INFO, "sched: thread %u affinity -> %d", tid, core);
        break;
    }
    spin_unlock_irqrestore(&sched_lock, flags);
}
int scheduler_get_core_count(void){ return (int)smp_online_count(); }
bool scheduler_is_started(void){ return started; }

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

static struct thread *pick_next(struct cpu_local *cpu){
    wake_sleeping_threads();
    int best=-1;
    int32_t best_score=0;
    bool has_best=false;
    for(int i=0;i<SCHEDULER_MAX_THREADS;i++){
        if(threads[i].state!=THREAD_READY) continue;
        if(threads[i].affinity>=0
           && threads[i].affinity!=(int16_t)cpu->index) continue;
        int32_t score=(int32_t)threads[i].waited
            -(int32_t)threads[i].priority*64;
        if(!has_best || score>best_score){
            has_best=true;
            best_score=score;
            best=i;
        }
    }
    if(best>=0){
        threads[best].waited=0;
        return &threads[best];
    }
    return cpu->idle;
}

static void activate_thread(struct thread *thread){
    gdt_set_kernel_stack((uint64_t)(uintptr_t)
                         (thread->stack+SCHEDULER_STACK_SIZE));
    vmm_switch_address_space(thread->address_space);
}

static bool thread_owns_stack(const struct thread *thread, uint64_t rsp){
    if(!thread) return false;
    uint64_t base = (uint64_t)(uintptr_t)thread->stack;
    return rsp >= base && rsp < base + SCHEDULER_STACK_SIZE;
}

static void check_live_stack(struct cpu_local *cpu, struct thread *prev){
    uint64_t live_rsp;
    __asm__ volatile("mov %%rsp,%0" : "=r"(live_rsp));
    if(!thread_canary_ok(prev) || !smp_this_ok()){
        char *p=sched_panic_reason;
        const char *prefix="sched: integrity fail cpu=";
        for(int i=0;prefix[i];i++) *p++=prefix[i];
        write_hex_digits(p, cpu->index, 2); p+=2;
        const char *mid=" prev=";
        for(int i=0;mid[i];i++) *p++=mid[i];
        write_hex_digits(p, prev ? prev->id : 0xFFFFFFFFu, 8); p+=8;
        const char *mid2=" rdpid=";
        for(int i=0;mid2[i];i++) *p++=mid2[i];
        write_hex_digits(p, smp_index_rdpid(), 8); p+=8;
        const char *mid3=" lapic=";
        for(int i=0;mid3[i];i++) *p++=mid3[i];
        write_hex_digits(p, smp_index_lapic(), 8); p+=8;
        *p='\0';
        spin_unlock(&sched_lock);
        __asm__ volatile("sti" ::: "memory");
        kernel_panic(sched_panic_reason);
    }
    if(thread_owns_stack(prev, live_rsp)) return;
    int32_t owner_id = -1;
    uint32_t owner_state = 99;
    int16_t owner_cpu = -1;
    for(int i = 0; i < SCHEDULER_MAX_THREADS; i++){
        if(thread_owns_stack(&threads[i], live_rsp)){
            owner_id = (int32_t)threads[i].id;
            owner_state = threads[i].state;
            owner_cpu = threads[i].last_cpu;
            break;
        }
    }
    char *p = sched_panic_reason;
    const char *prefix = "sched: live RSP mismatch cpu=";
    for(int i = 0; prefix[i]; i++) *p++ = prefix[i];
    write_hex_digits(p, cpu->index, 2); p += 2;
    const char *mid = " prev=";
    for(int i = 0; mid[i]; i++) *p++ = mid[i];
    write_hex_digits(p, prev ? prev->id : 0xFFFFFFFFu, 8); p += 8;
    const char *mid2 = " live=";
    for(int i = 0; mid2[i]; i++) *p++ = mid2[i];
    write_hex_digits(p, live_rsp, 16); p += 16;
    const char *mid3 = " base=";
    for(int i = 0; mid3[i]; i++) *p++ = mid3[i];
    write_hex_digits(p, (uint64_t)(uintptr_t)prev->stack, 16); p += 16;
    const char *mid4 = " prevcpu=";
    for(int i = 0; mid4[i]; i++) *p++ = mid4[i];
    write_hex_digits(p, (uint32_t)(uint16_t)prev->last_cpu, 4); p += 4;
    const char *mid5 = " owner=";
    for(int i = 0; mid5[i]; i++) *p++ = mid5[i];
    write_hex_digits(p, (uint32_t)owner_id, 8); p += 8;
    const char *mid6 = " ostate=";
    for(int i = 0; mid6[i]; i++) *p++ = mid6[i];
    write_hex_digits(p, owner_state, 2); p += 2;
    const char *mid7 = " ocpu=";
    for(int i = 0; mid7[i]; i++) *p++ = mid7[i];
    write_hex_digits(p, (uint32_t)(uint16_t)owner_cpu, 4); p += 4;
    const char *mid8 = " pstate=";
    for(int i = 0; mid8[i]; i++) *p++ = mid8[i];
    write_hex_digits(p, prev->state, 2); p += 2;
    const char *mid9 = " plast=";
    for(int i = 0; mid9[i]; i++) *p++ = mid9[i];
    write_hex_digits(p, (uint32_t)(uint16_t)prev->last_cpu, 4); p += 4;
    for(uint32_t ci = 0; ci < SMP_MAX_CPUS; ci++){
        struct cpu_local *c = smp_cpu(ci);
        if(!c || !c->present) continue;
        const char *midc = " c";
        for(int i = 0; midc[i]; i++) *p++ = midc[i];
        write_hex_digits(p, ci, 2); p += 2;
        const char *mide = "=";
        for(int i = 0; mide[i]; i++) *p++ = mide[i];
        write_hex_digits(p, c->current ? c->current->id : 0xFFFFFFFFu, 8);
        p += 8;
    }
    *p = '\0';
    spin_unlock(&sched_lock);
    __asm__ volatile("sti" ::: "memory");
    kernel_panic(sched_panic_reason);
}

static void check_no_double_dispatch(struct cpu_local *cpu,
                                        struct thread *next){
    for(uint32_t i = 0; i < SMP_MAX_CPUS; i++){
        struct cpu_local *c = smp_cpu(i);
        if(!c || !c->present || i == cpu->index) continue;
        if(c->current == next){
            char *p=sched_panic_reason;
            const char *prefix="sched: double dispatch tgt=";
            for(int k=0;prefix[k];k++) *p++=prefix[k];
            write_hex_digits(p, next ? next->id : 0xFFFFFFFFu, 8); p+=8;
            const char *mid=" by="; for(int k=0;mid[k];k++) *p++=mid[k];
            write_hex_digits(p, cpu->index, 2); p+=2;
            const char *mid2=" holder="; for(int k=0;mid2[k];k++) *p++=mid2[k];
            write_hex_digits(p, i, 2); p+=2;
            *p='\0';
            spin_unlock(&sched_lock);
            __asm__ volatile("sti" ::: "memory");
            kernel_panic(sched_panic_reason);
        }
    }
}

static void schedule_locked(struct cpu_local *cpu, int prev_state,
                            uint64_t flags, bool restore_flags){
    struct thread *prev = cpu->current;
    if(!prev){
        spin_unlock(&sched_lock);
        if(restore_flags) irq_restore(flags);
        return;
    }
    if(prev->state==THREAD_RUNNING) prev->state=(uint8_t)prev_state;
    struct thread *next = pick_next(cpu);
    if(!next) next = cpu->idle;
    next->state=THREAD_RUNNING;
    next->ticks_remaining=SCHEDULER_TIME_SLICE_MS;
    next->last_cpu=(int16_t)cpu->index;
    check_no_double_dispatch(cpu, next);
    cpu->current=next;
    if(next!=prev){
        activate_thread(next);
        validate_switch_target(prev, next);
        check_live_stack(cpu, prev);
        fpu_save(prev->fpu_state);
        fpu_restore(next->fpu_state);
    }
    spin_unlock(&sched_lock);
    if(next!=prev)
        scheduler_asm_switch(&prev->rsp, &next->rsp);
    if(restore_flags) irq_restore(flags);
}

void scheduler_yield(void){
    if(!initialized) return;
    struct cpu_local *cpu = smp_this();
    uint64_t flags = spin_lock_irqsave(&sched_lock);
    schedule_locked(cpu, THREAD_READY, flags, true);
}

void scheduler_sleep(uint32_t milliseconds){
    if(!initialized || milliseconds==0){
        scheduler_yield();
        return;
    }
    struct cpu_local *cpu = smp_this();
    if(!cpu->current){
        scheduler_yield();
        return;
    }
    uint64_t now=timer_ticks();
    uint64_t wake=now+milliseconds;
    uint64_t flags = spin_lock_irqsave(&sched_lock);
    cpu->current->wake_tick=wake<now ? UINT64_MAX : wake;
    schedule_locked(cpu, THREAD_BLOCKED, flags, true);
}

void scheduler_block(void){
    struct cpu_local *cpu = smp_this();
    if(!cpu->current || cpu->current==cpu->idle) return;
    uint64_t flags = spin_lock_irqsave(&sched_lock);
    schedule_locked(cpu, THREAD_BLOCKED, flags, true);
}

void scheduler_unblock(int tid){
    uint64_t flags = spin_lock_irqsave(&sched_lock);
    for(int i=0;i<SCHEDULER_MAX_THREADS;i++) if(threads[i].id==(uint32_t)tid){
        if(threads[i].state==THREAD_BLOCKED && threads[i].wake_tick==0){
            threads[i].state=THREAD_READY;
            klogf(KLOG_DEBUG, "sched: thread %u unblocked", tid);
        }
        break;
    }
    spin_unlock_irqrestore(&sched_lock, flags);
}

void scheduler_wait_queued(void){
    struct cpu_local *cpu = smp_this();
    if(!cpu->current) return;
    for(;;){
        uint64_t flags = spin_lock_irqsave(&sched_lock);
        if(cpu->current->wait_woken){
            cpu->current->wait_woken=false;
            spin_unlock_irqrestore(&sched_lock, flags);
            return;
        }
        schedule_locked(cpu, THREAD_BLOCKED, flags, true);
    }
}

void scheduler_make_ready(struct thread *thread){
    if(!thread) return;
    uint64_t flags = spin_lock_irqsave(&sched_lock);
    if(thread->state==THREAD_BLOCKED) thread->state=THREAD_READY;
    spin_unlock_irqrestore(&sched_lock, flags);
}

void scheduler_exit(void){
    struct cpu_local *cpu = smp_this();
    if(!cpu->current) for(;;) __asm__ volatile("cli; hlt");
    if(cpu->current==cpu->idle) for(;;) __asm__ volatile("hlt");
    uint64_t flags = spin_lock_irqsave(&sched_lock);
    klogf(KLOG_INFO, "sched: thread %u (%s) exiting", cpu->current->id, cpu->current->name);
    schedule_locked(cpu, THREAD_TERMINATED, flags, true);
    for(;;) __asm__ volatile("hlt");
}

void scheduler_on_timer_interrupt(void){
    if(!started) return;
    struct cpu_local *cpu = smp_this();
    spin_lock(&sched_lock);
    total_ticks++;
    cpu->timer_ticks++;
    struct thread *current = cpu->current;
    if(!current){
        spin_unlock(&sched_lock);
        return;
    }
    current->runtime_ticks++;
    if(current==cpu->idle) idle_ticks++;
    for(int i=0;i<SCHEDULER_MAX_THREADS;i++)
        if(threads[i].state==THREAD_READY && threads[i].waited<UINT32_MAX)
            threads[i].waited++;
    wake_sleeping_threads();
    bool reschedule = false;
    if(current->state!=THREAD_RUNNING) reschedule = true;
    else if(current->ticks_remaining>0){
        current->ticks_remaining--;
        if(current->ticks_remaining==0) reschedule = true;
    } else reschedule = true;
    if(!reschedule){
        spin_unlock(&sched_lock);
        return;
    }
    if(current->state==THREAD_RUNNING) current->state=THREAD_READY;
    struct thread *next = pick_next(cpu);
    if(!next) next = cpu->idle;
    next->state=THREAD_RUNNING;
    next->ticks_remaining=SCHEDULER_TIME_SLICE_MS;
    next->last_cpu=(int16_t)cpu->index;
    check_no_double_dispatch(cpu, next);
    cpu->current=next;
    if(next==current){
        spin_unlock(&sched_lock);
        return;
    }
    activate_thread(next);
    validate_switch_target(current, next);
    check_live_stack(cpu, current);
    fpu_save(current->fpu_state);
    fpu_restore(next->fpu_state);
    spin_unlock(&sched_lock);
    scheduler_asm_switch(&current->rsp, &next->rsp);
}

static int create_idle_thread(uint32_t cpu){
    char name[32];
    name[0]='i'; name[1]='d'; name[2]='l'; name[3]='e';
    name[4]=(char)('0'+(cpu/10)%10);
    name[5]=(char)('0'+cpu%10);
    name[6]='\0';
    return create_thread(scheduler_idle_entry, smp_cpu(cpu), name, 7,
                         (int16_t)cpu, vmm_kernel_address_space(), 0, false);
}

void scheduler_enter(void){
    struct cpu_local *cpu = smp_this();
    if(!cpu->idle){
        int id = create_idle_thread(cpu->index);
        uint64_t found_flags = spin_lock_irqsave(&sched_lock);
        for(int i=0;i<SCHEDULER_MAX_THREADS;i++)
            if(threads[i].id==(uint32_t)id) cpu->idle=&threads[i];
        spin_unlock_irqrestore(&sched_lock, found_flags);
    }
    if(!cpu->current) cpu->current=cpu->idle;
    uint64_t flags = spin_lock_irqsave(&sched_lock);
    struct thread *next = pick_next(cpu);
    if(!next) next = cpu->idle;
    next->state=THREAD_RUNNING;
    next->ticks_remaining=SCHEDULER_TIME_SLICE_MS;
    next->last_cpu=(int16_t)cpu->index;
    check_no_double_dispatch(cpu, next);
    cpu->current=next;
    activate_thread(next);
    validate_switch_target(NULL, next);
    fpu_restore(next->fpu_state);
    spin_unlock(&sched_lock);
    (void)flags;
    uint64_t discard;
    scheduler_asm_switch(&discard, &next->rsp);
    for(;;) __asm__ volatile("cli; hlt");
}

void scheduler_start(void){
    if(!initialized) for(;;) __asm__ volatile("cli; hlt");
    klogf(KLOG_INFO, "sched: starting with %u threads on %u CPUs", scheduler_thread_count(), smp_online_count());
    started=true;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    scheduler_enter();
}
