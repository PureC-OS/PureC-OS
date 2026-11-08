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
static void dummy_entry(void *arg){ (void)arg; }

static void startn(unsigned count){
    scheduler_host_reset();
    mock_ticks = 100;
    for(unsigned i = 0; i < 4; i++){
        executing_cpu = i;
        scheduler_init_cpu();
        cpu_schedulers[i].started = i < count;
    }
    executing_cpu = 0;
}

static struct thread *spawn(int16_t affinity){
    int id = scheduler_create_thread(dummy_entry, NULL, "t", 1, affinity);
    assert(id >= 0);
    struct thread *t = scheduler_host_lookup(id);
    assert(t);
    return t;
}

static void make_current(unsigned cpu, struct thread *t){
    scheduler_host_dequeue(t);
    cpu_schedulers[cpu].current = t;
    t->state = THREAD_RUNNING;
    t->running_cpu = (int16_t)cpu;
}

static uint32_t queued(unsigned cpu){ return cpu_schedulers[cpu].rq_count; }

static void test_placement_spreads(void){
    startn(4);
    for(int i = 0; i < 8; i++) (void)spawn(-1);
    for(unsigned c = 0; c < 4; c++) assert(queued(c) == 2);
}

static void test_placement_respects_affinity_and_kernel_only(void){
    startn(4);
    struct thread *pinned = spawn(3);
    assert(pinned->rq_cpu == 3);
    int id = scheduler_create_user_thread(dummy_entry, NULL, "u", 1, -1, 0x1000,
                                          (struct process *)&pinned);
    assert(id >= 0);
    struct thread *user = scheduler_host_lookup(id);
    assert(user->kernel_only);
    assert(user->rq_cpu == 0);
}

static void test_idle_pull_balances(void){
    startn(1);
    struct thread *t[4];
    for(int i = 0; i < 4; i++) t[i] = spawn(-1);
    assert(queued(0) == 4);
    for(unsigned c = 1; c < 4; c++) cpu_schedulers[c].started = true;
    executing_cpu = 1;
    struct thread *p = pick_next();
    assert(!p->idle);
    assert(p->rq_cpu == 1);
    assert(queued(0) == 3 && queued(1) == 1);
    scheduler_host_dequeue(p);
    cpu_schedulers[1].current = p;
    p->state = THREAD_RUNNING;
    p->running_cpu = 1;
    (void)t;
    executing_cpu = 2;
    struct thread *q = pick_next();
    assert(!q->idle && q != p);
    assert(queued(0) == 2);
    executing_cpu = 0;
}

static void test_no_steal_of_lone_thread(void){
    startn(2);
    struct thread *t = spawn(-1);
    assert(t->rq_cpu == 0 || t->rq_cpu == 1);
    unsigned owner = (unsigned)t->rq_cpu;
    unsigned other = owner ? 0u : 1u;
    executing_cpu = other;
    assert(pick_next()->idle);
    assert(t->rq_cpu == (int16_t)owner);
    executing_cpu = 0;
}

static void test_periodic_balance_and_stability(void){
    startn(2);
    struct thread *cur0 = spawn(0);
    make_current(0, cur0);
    for(int i = 0; i < 3; i++){
        struct thread *w = spawn(0);
        w->affinity = -1;
        w->last_ran = 0;
    }
    struct thread *cur1 = spawn(1);
    make_current(1, cur1);
    assert(queued(0) == 3 && queued(1) == 0);
    executing_cpu = 1;
    mock_ticks = 1000;
    struct thread *moved = pull_task(1, false);
    assert(moved && moved->rq_cpu == 1);
    assert(queued(0) == 2 && queued(1) == 1);
    assert(pull_task(1, false) == NULL);
    assert(queued(0) == 2 && queued(1) == 1);
    executing_cpu = 0;
}

static void test_cache_hot_not_pulled_by_periodic(void){
    startn(2);
    struct thread *cur0 = spawn(0);
    make_current(0, cur0);
    struct thread *hot[3];
    for(int i = 0; i < 3; i++){
        hot[i] = spawn(0);
        hot[i]->affinity = -1;
        hot[i]->last_ran = 999;
    }
    struct thread *cur1 = spawn(1);
    make_current(1, cur1);
    executing_cpu = 1;
    mock_ticks = 1000;
    assert(pull_task(1, false) == NULL);
    assert(queued(0) == 3);
    struct thread *idle_pulled = pull_task(1, true);
    assert(idle_pulled && idle_pulled->rq_cpu == 1);
    executing_cpu = 0;
}

static void test_pinned_never_pulled(void){
    startn(3);
    struct thread *cur0 = spawn(0);
    make_current(0, cur0);
    for(int i = 0; i < 4; i++) (void)spawn(0);
    executing_cpu = 2;
    assert(pick_next()->idle);
    assert(queued(0) == 4 && queued(2) == 0);
    executing_cpu = 0;
}

static void test_vruntime_normalised_on_pull(void){
    startn(2);
    struct thread *cur0 = spawn(0);
    make_current(0, cur0);
    cpu_schedulers[0].rq_min_vruntime = 5000;
    struct thread *a = spawn(0);
    a->affinity = -1;
    a->virtual_runtime = 5300;
    struct thread *b = spawn(0);
    b->affinity = -1;
    b->virtual_runtime = 5100;
    cpu_schedulers[1].rq_min_vruntime = 100;
    executing_cpu = 1;
    struct thread *moved = pull_task(1, true);
    assert(moved == a);
    assert(moved->virtual_runtime == 100 + 300);
    executing_cpu = 0;
}

static void test_sleepers_wake_in_order(void){
    startn(2);
    struct thread *t1 = spawn(-1);
    struct thread *t2 = spawn(-1);
    struct thread *t3 = spawn(-1);
    scheduler_host_sleep_thread(t1, 150);
    scheduler_host_sleep_thread(t2, 120);
    scheduler_host_sleep_thread(t3, 300);
    assert(t1->state == THREAD_BLOCKED && t2->state == THREAD_BLOCKED);
    assert(sleep_head == t2 && t2->sleep_next == t1 && t1->sleep_next == t3);
    mock_ticks = 125;
    struct scheduler_cpu *cpu = &cpu_schedulers[0];
    uint64_t flags = spin_lock_irqsave(&runqueue_lock);
    wake_expired_sleepers(cpu, 0);
    spin_unlock_irqrestore(&runqueue_lock, flags);
    assert(t2->state == THREAD_READY && t2->rq_cpu >= 0);
    assert(t1->state == THREAD_BLOCKED && t3->state == THREAD_BLOCKED);
    mock_ticks = 400;
    flags = spin_lock_irqsave(&runqueue_lock);
    wake_expired_sleepers(cpu, 0);
    spin_unlock_irqrestore(&runqueue_lock, flags);
    assert(t1->state == THREAD_READY && t3->state == THREAD_READY);
    assert(sleep_head == NULL);
}

static void test_unblock_removes_from_sleep_list(void){
    startn(2);
    struct thread *t = spawn(-1);
    scheduler_host_sleep_thread(t, 500);
    assert(sleep_head == t);
    scheduler_unblock((int)t->id);
    assert(sleep_head == NULL);
    assert(t->state == THREAD_READY && t->rq_cpu >= 0 && t->wake_tick == 0);
}

static void test_wake_prefers_last_cpu(void){
    startn(3);
    struct thread *t = spawn(-1);
    scheduler_host_dequeue(t);
    t->last_cpu = 2;
    t->state = THREAD_RUNNING;
    scheduler_host_sleep_thread(t, 200);
    mock_ticks = 300;
    uint64_t flags = spin_lock_irqsave(&runqueue_lock);
    wake_expired_sleepers(&cpu_schedulers[0], 0);
    spin_unlock_irqrestore(&runqueue_lock, flags);
    assert(t->rq_cpu == 2);
}

static void test_queue_integrity(void){
    startn(4);
    struct thread *t[32];
    for(int i = 0; i < 32; i++) t[i] = spawn(-1);
    for(int i = 0; i < 32; i += 3) scheduler_host_dequeue(t[i]);
    uint32_t total = 0;
    uint64_t weight = 0;
    for(unsigned c = 0; c < 4; c++){
        uint32_t n = 0;
        for(struct thread *q = cpu_schedulers[c].rq_head; q; q = q->rq_next){
            assert(q->rq_cpu == (int16_t)c);
            n++;
            weight += thread_weight(q);
        }
        assert(n == cpu_schedulers[c].rq_count);
        total += n;
    }
    assert(total == 32 - 11);
    uint64_t recorded = 0;
    for(unsigned c = 0; c < 4; c++) recorded += cpu_schedulers[c].rq_weight;
    assert(recorded == weight);
}

int main(void){
    test_placement_spreads();
    test_placement_respects_affinity_and_kernel_only();
    test_idle_pull_balances();
    test_no_steal_of_lone_thread();
    test_periodic_balance_and_stability();
    test_cache_hot_not_pulled_by_periodic();
    test_pinned_never_pulled();
    test_vruntime_normalised_on_pull();
    test_sleepers_wake_in_order();
    test_unblock_removes_from_sleep_list();
    test_wake_prefers_last_cpu();
    test_queue_integrity();
    puts("Scheduler run queue placement, balancing and sleeper tests passed");
    return 0;
}
