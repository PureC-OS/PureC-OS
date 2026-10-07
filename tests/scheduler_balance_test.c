#include "kernel/process/scheduler.c"
#include "kernel/diagnostics/klog.h"
#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define BAL_POOL_PAGES 256
static uint8_t bal_pool[BAL_POOL_PAGES * 4096];
static bool bal_used[BAL_POOL_PAGES];
static uint64_t bal_free_count = BAL_POOL_PAGES;
static _Thread_local uint32_t executing_cpu;
static uint64_t mock_ticks = 100;
uint64_t timer_ticks(void){ return mock_ticks; }
uint32_t gdt_current_cpu_id(void){ return executing_cpu; }
void kernel_panic(const char *reason){ (void)reason; abort(); }
void klogf(enum klog_level lvl, const char *fmt, ...){ (void)lvl; (void)fmt; }
void fpu_save(void *area){ (void)area; }
void fpu_restore(const void *area){ (void)area; }
void gdt_set_kernel_stack(uint64_t stack_top){ (void)stack_top; }
uint64_t vmm_kernel_address_space(void){ return 0x1000; }
void fpu_thread_init(void *area){ (void)area; }
void vmm_switch_address_space(uint64_t address_space){ (void)address_space; }
void scheduler_asm_switch(uint64_t *old_rsp, uint64_t *new_rsp){ (void)old_rsp; (void)new_rsp; }
void smp_reschedule_all(void){}
void smp_reschedule_cpu(uint32_t id){ (void)id; }
uint32_t cpu_registered_count(void){ return 16; }
bool cpu_is_online(uint32_t id){ (void)id; return true; }
uint64_t pmm_allocate_page(void){
    for(uint32_t i = 0; i < BAL_POOL_PAGES; i++){
        if(!bal_used[i]){
            bal_used[i] = true;
            bal_free_count--;
            memset(bal_pool + (uint64_t)i * 4096, 0, 4096);
            return ((uint64_t)i + 1) * 4096;
        }
    }
    return 0;
}
uint64_t pmm_allocate_contiguous(uint64_t count){
    if(!count || count > BAL_POOL_PAGES) return 0;
    for(uint32_t i = 0; i + count <= BAL_POOL_PAGES; i++){
        uint32_t j = 0;
        while(j < count && !bal_used[i + j]) j++;
        if(j == count){
            for(uint32_t k = 0; k < count; k++){
                bal_used[i + k] = true;
                memset(bal_pool + (uint64_t)(i + k) * 4096, 0, 4096);
            }
            bal_free_count -= count;
            return ((uint64_t)i + 1) * 4096;
        }
        i += j;
    }
    return 0;
}
void *pmm_physical_to_virtual(uint64_t phys){
    if(!phys) return NULL;
    uint32_t i = (uint32_t)(phys / 4096) - 1;
    if(i >= BAL_POOL_PAGES) return NULL;
    return bal_pool + (uint64_t)i * 4096;
}
void pmm_free_page(uint64_t phys){
    if(!phys) return;
    uint32_t i = (uint32_t)(phys / 4096) - 1;
    if(i < BAL_POOL_PAGES && bal_used[i]){ bal_used[i] = false; bal_free_count++; }
}
void pmm_free_contiguous(uint64_t phys, uint64_t count){
    if(!phys) return;
    uint32_t i = (uint32_t)(phys / 4096) - 1;
    for(uint64_t k = 0; k < count && i + k < BAL_POOL_PAGES; k++){
        if(bal_used[i + k]){ bal_used[i + k] = false; bal_free_count++; }
    }
}
static int dummy_proc;
static void dummy_entry(void *arg){ (void)arg; }
static void reset4(void){
    scheduler_host_reset();
    for(unsigned i = 0; i < 4; i++){
        executing_cpu = i;
        scheduler_init_cpu();
        cpu_schedulers[i].started = true;
    }
    executing_cpu = 0;
}
static int make_user(void){
    int id = scheduler_create_user_thread(dummy_entry, NULL, "u", 1, -1, 0x1000, (struct process *)&dummy_proc);
    assert(id >= 0);
    return id;
}
static void test_spill_to_idle_ap(void){
    reset4();
    int id = make_user();
    struct thread *t = scheduler_host_lookup(id);
    assert(t != NULL);
    assert(t->kernel_only);
    struct scheduler_cpu *c0 = &cpu_schedulers[0];
    c0->current = t;
    t->state = THREAD_RUNNING;
    t->running_cpu = 0;
    c0->preempt_depth = 1;
    scheduler_leave_kernel();
    assert(!t->kernel_only);
    assert(t->affinity == 1);
    assert(t->affinity_auto);
    assert(c0->reschedule_pending);
    c0->preempt_depth = 0;
    c0->reschedule_pending = false;
    t->state = THREAD_READY;
    t->running_cpu = -1;
    executing_cpu = 1;
    assert(pick_next() == t);
    executing_cpu = 0;
}
static void test_explicit_pin_kept(void){
    reset4();
    int id = make_user();
    struct thread *t = scheduler_host_lookup(id);
    scheduler_set_affinity(id, 2);
    assert(t->affinity == 2);
    assert(!t->affinity_auto);
    struct scheduler_cpu *c0 = &cpu_schedulers[0];
    c0->current = t;
    t->state = THREAD_RUNNING;
    t->running_cpu = 0;
    c0->preempt_depth = 1;
    scheduler_leave_kernel();
    assert(t->affinity == 2);
    assert(!t->affinity_auto);
    c0->preempt_depth = 0;
    c0->reschedule_pending = false;
}
static void test_no_idle_stays(void){
    reset4();
    int ids[3];
    for(int i = 0; i < 3; i++){
        ids[i] = make_user();
        struct thread *w = scheduler_host_lookup(ids[i]);
        w->affinity = (int16_t)(i + 1);
        w->affinity_auto = false;
        w->state = THREAD_RUNNING;
        w->running_cpu = (int16_t)(i + 1);
        cpu_schedulers[i + 1].current = w;
    }
    int id = make_user();
    struct thread *t = scheduler_host_lookup(id);
    struct scheduler_cpu *c0 = &cpu_schedulers[0];
    c0->current = t;
    t->state = THREAD_RUNNING;
    t->running_cpu = 0;
    c0->preempt_depth = 1;
    scheduler_leave_kernel();
    assert(t->affinity == -1);
    assert(!t->affinity_auto);
    assert(!c0->reschedule_pending);
    c0->preempt_depth = 0;
}
static void test_enter_pins_bsp(void){
    reset4();
    int id = make_user();
    struct thread *t = scheduler_host_lookup(id);
    t->affinity = -1;
    cpu_schedulers[1].current = t;
    t->state = THREAD_RUNNING;
    t->running_cpu = 1;
    executing_cpu = 1;
    cpu_schedulers[1].preempt_depth = 1;
    scheduler_enter_kernel();
    assert(t->kernel_only);
    assert(cpu_schedulers[1].reschedule_pending);
    cpu_schedulers[1].preempt_depth = 0;
    cpu_schedulers[1].reschedule_pending = false;
    executing_cpu = 0;
}
int main(void){
    test_spill_to_idle_ap();
    test_explicit_pin_kept();
    test_no_idle_stays();
    test_enter_pins_bsp();
    puts("Scheduler syscall spillover balancing tests passed");
    return 0;
}
