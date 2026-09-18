/* Hardware context switching is covered by the QEMU boot self-test. Here the
   real selection/allocation logic runs concurrently with a mocked CPU ID. */
#include "kernel/process/scheduler.c"
#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>

static _Thread_local uint32_t executing_cpu;
uint64_t timer_ticks(void){ return 100; }
uint32_t gdt_current_cpu_id(void){ return executing_cpu; }
void kernel_panic(const char *reason){ (void)reason; abort(); }

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
int main(void){
    test_selection();
    test_concurrent_claims();
    puts("Scheduler affinity, ownership, wakeup and concurrent selection tests passed");
    return 0;
}
