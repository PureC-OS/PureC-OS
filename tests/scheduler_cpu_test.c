#include "kernel/process/scheduler.c"
#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>

static _Thread_local uint32_t executing_cpu;
static uint64_t mock_ticks=100;
uint64_t timer_ticks(void){ return mock_ticks; }
uint32_t gdt_current_cpu_id(void){ return executing_cpu; }
void kernel_panic(const char *reason){ (void)reason; abort(); }
void fpu_save(void *area){ (void)area; }
void fpu_restore(const void *area){ (void)area; }
void gdt_set_kernel_stack(uint64_t stack_top){ (void)stack_top; }
void vmm_switch_address_space(uint64_t address_space){ (void)address_space; }
void scheduler_asm_switch(uint64_t *old_rsp, uint64_t *new_rsp){
    (void)old_rsp;
    (void)new_rsp;
    abort();
}

static void reset(void){
    memset(threads,0,sizeof(threads));
    memset(cpu_schedulers,0,sizeof(cpu_schedulers));
    memset(idle_threads,0,sizeof(idle_threads));
    for(unsigned i=0;i<SCHEDULER_MAX_THREADS;i++){
        threads[i].affinity=-1;
        threads[i].running_cpu=-1;
        threads[i].id=i+1;
    }
    for(unsigned i=0;i<CPU_MAX_COUNT;i++){
        idle_threads[i].idle=true;
        idle_threads[i].state=THREAD_RUNNING;
        cpu_schedulers[i].current=&idle_threads[i];
        cpu_schedulers[i].idle=&idle_threads[i];
        cpu_schedulers[i].cursor=SCHEDULER_MAX_THREADS-1;
    }
}
static void test_selection(void){
    reset();
    executing_cpu=UINT32_MAX;
    assert(scheduler_current_thread()==NULL);
    executing_cpu=0;
    threads[0].state=THREAD_READY;
    threads[0].affinity=1;
    assert(pick_next()==&idle_threads[0]);
    executing_cpu=1;
    assert(pick_next()==&threads[0]);
    threads[0].running_cpu=0; /* Outgoing stack not saved yet: cannot steal. */
    assert(pick_next()==&idle_threads[1]);
    threads[0].running_cpu=-1;
    threads[0].kernel_only=true;
    assert(pick_next()==&idle_threads[1]);
    executing_cpu=0;
    assert(pick_next()==&threads[0]); /* Service override preserves affinity. */
    assert(threads[0].affinity==1);
    threads[0].state=THREAD_BLOCKED;
    threads[0].wake_tick=101;
    assert(pick_next()==&idle_threads[0]);
    threads[0].wake_tick=100;
    assert(pick_next()==&threads[0]);
    assert(threads[0].wake_tick==0);
    threads[0].state=THREAD_TERMINATED;
    threads[0].running_cpu=1;
    assert(alloc_thread()!=&threads[0]);
    threads[0].running_cpu=-1;
    assert(alloc_thread()==&threads[0]);
}
static unsigned claims[SCHEDULER_MAX_THREADS];
static void *claim_threads(void *arg){
    executing_cpu=(uint32_t)(uintptr_t)arg;
    for(;;){
        spin_lock(&runqueue_lock);
        struct thread *t=pick_next();
        if(t->idle){ spin_unlock(&runqueue_lock); break; }
        t->state=THREAD_RUNNING;
        t->running_cpu=(int16_t)executing_cpu;
        claims[t-threads]++;
        spin_unlock(&runqueue_lock);
    }
    return NULL;
}
static void test_concurrent_claims(void){
    reset();
    for(unsigned i=0;i<SCHEDULER_MAX_THREADS;i++) threads[i].state=THREAD_READY;
    pthread_t workers[CPU_MAX_COUNT];
    for(unsigned i=0;i<CPU_MAX_COUNT;i++) assert(!pthread_create(&workers[i],NULL,claim_threads,(void*)(uintptr_t)i));
    for(unsigned i=0;i<CPU_MAX_COUNT;i++) assert(!pthread_join(workers[i],NULL));
    for(unsigned i=0;i<SCHEDULER_MAX_THREADS;i++) assert(claims[i]==1);
}
static void test_preemption_guard(void){
    reset();
    executing_cpu=0;
    struct scheduler_cpu *cpu=&cpu_schedulers[0];
    cpu->initialized=true;
    cpu->started=true;
    cpu->last_tick=100;
    cpu->current=&threads[0];
    threads[0].state=THREAD_RUNNING;
    threads[0].ticks_remaining=1;

    assert(scheduler_preempt_disable());
    assert(scheduler_preempt_disable());
    mock_ticks=101;
    scheduler_on_timer_interrupt();
    assert(cpu->current==&threads[0]);
    assert(cpu->preempt_depth==2);
    assert(cpu->reschedule_pending);

    scheduler_preempt_enable();
    assert(cpu->preempt_depth==1);
    assert(cpu->reschedule_pending);
    scheduler_preempt_enable();
    assert(cpu->preempt_depth==0);
    assert(!cpu->reschedule_pending);
    assert(cpu->current==&threads[0]);
    assert(threads[0].ticks_remaining==SCHEDULER_TIME_SLICE_MS);
    mock_ticks=100;
}
int main(void){
    test_selection();
    test_concurrent_claims();
    test_preemption_guard();
    puts("Scheduler affinity, ownership, wakeup and concurrent selection tests passed");
    return 0;
}
