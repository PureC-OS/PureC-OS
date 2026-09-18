/* Keep the CPU-local state private in production while testing its isolation.
   Unused hardware switching paths are removed by --gc-sections. */
#include "kernel/process/scheduler.c"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

static uint32_t executing_cpu;
uint64_t timer_ticks(void){ return 100; }
uint32_t gdt_current_cpu_id(void){ return executing_cpu; }
void kernel_panic(const char *reason){ (void)reason; abort(); }

int main(void){
    struct thread bsp = {.id = 42};
    struct thread ap = {.id = 77};
    executing_cpu = UINT32_MAX;
    assert(scheduler_current_thread() == NULL);
    assert(scheduler_current_tid() == -1);
    executing_cpu = 0;
    local_scheduler()->current = &bsp;
    local_scheduler()->started = true;
    local_scheduler()->need_resched = true;
    local_scheduler()->total_ticks = 123;
    local_scheduler()->idle_ticks = 9;
    assert(scheduler_current_thread() == &bsp);
    assert(scheduler_current_tid() == 42);

    for(executing_cpu = 1; executing_cpu < CPU_MAX_COUNT; executing_cpu++){
        assert(scheduler_current_thread() == NULL);
        assert(scheduler_current_tid() == -1);
        assert(!local_scheduler()->started);
        assert(!local_scheduler()->need_resched);
        assert(scheduler_total_ticks() == 0);
        assert(scheduler_idle_ticks() == 0);
    }
    executing_cpu = 1;
    local_scheduler()->current = &ap;
    local_scheduler()->total_ticks = 456;
    assert(scheduler_current_tid() == 77);
    executing_cpu = 0;
    assert(scheduler_current_thread() == &bsp);
    assert(local_scheduler()->started && local_scheduler()->need_resched);
    assert(scheduler_total_ticks() == 123);
    assert(scheduler_idle_ticks() == 9);
    /* Selection uses this CPU's idle fallback and preserves its state until
       the caller commits the switch. Round-robin skips blocked threads. */
    threads[1].state = THREAD_BLOCKED;
    threads[2].state = THREAD_READY;
    threads[2].id = 2;
    threads[3].state = THREAD_READY;
    threads[3].id = 3;
    local_scheduler()->current = &threads[1];
    assert(pick_next() == &threads[2]);
    local_scheduler()->current = &threads[2];
    assert(pick_next() == &threads[3]);
    threads[2].state = THREAD_BLOCKED;
    threads[3].state = THREAD_BLOCKED;
    bsp.state = THREAD_READY;
    local_scheduler()->idle = &bsp;
    assert(pick_next() == &bsp);
    assert(bsp.state == THREAD_READY);
    executing_cpu = 1;
    ap.state = THREAD_BLOCKED;
    local_scheduler()->idle = &ap;
    assert(pick_next() == &ap);
    puts("Scheduler CPU isolation tests passed");
    return 0;
}
