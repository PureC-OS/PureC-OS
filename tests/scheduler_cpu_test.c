#include "kernel/process/scheduler.c"
#include "kernel/diagnostics/klog.h"
#include <assert.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>

static _Thread_local uint32_t executing_cpu;
static uint64_t mock_ticks=100;
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
void scheduler_asm_switch(uint64_t *old_rsp, uint64_t *new_rsp){
    (void)old_rsp;
    (void)new_rsp;
    abort();
}
void smp_reschedule_all(void){}
void smp_reschedule_cpu(uint32_t id){ (void)id; }
uint32_t cpu_registered_count(void){ return 16; }
bool cpu_is_online(uint32_t id){ (void)id; return true; }

#define MOCK_MAX_PAGES 8192
static uint64_t mock_phys[MOCK_MAX_PAGES];
static void *mock_ptr[MOCK_MAX_PAGES];
static uint32_t mock_pages;
static uint64_t mock_next_phys=0x100000ULL;
static int mock_fail_after=-1;

static void *mock_alloc_pages(uint64_t count){
    if(mock_fail_after>=0){
        if(mock_fail_after<(int)count) return NULL;
        mock_fail_after-=(int)count;
    }
    if(mock_pages+count>MOCK_MAX_PAGES) return NULL;
    void *base=aligned_alloc(4096,(size_t)(count*4096));
    if(!base) return NULL;
    memset(base,0,(size_t)(count*4096));
    for(uint64_t i=0;i<count;i++){
        mock_phys[mock_pages]=(mock_next_phys+(uint64_t)mock_pages*4096);
        mock_ptr[mock_pages]=(uint8_t*)base+i*4096;
        mock_pages++;
    }
    return base;
}

uint64_t pmm_allocate_page(void){
    return mock_alloc_pages(1) ? mock_phys[mock_pages-1] : 0;
}
uint64_t pmm_allocate_contiguous(uint64_t count){
    if(!count) return 0;
    uint32_t before=mock_pages;
    if(!mock_alloc_pages(count)) return 0;
    return mock_phys[before];
}
void *pmm_physical_to_virtual(uint64_t phys){
    for(uint32_t i=0;i<mock_pages;i++)
        if(mock_phys[i]==phys) return mock_ptr[i];
    return NULL;
}
static void mock_forget(uint64_t phys){
    for(uint32_t i=0;i<mock_pages;i++){
        if(mock_phys[i]==phys){
            free((void*)((uintptr_t)mock_ptr[i]&~(uintptr_t)4095));
            mock_phys[i]=mock_phys[mock_pages-1];
            mock_ptr[i]=mock_ptr[mock_pages-1];
            mock_pages--;
            return;
        }
    }
}
void pmm_free_page(uint64_t phys){ mock_forget(phys); }
void pmm_free_contiguous(uint64_t phys, uint64_t count){
    for(uint64_t i=0;i<count;i++) mock_forget(phys+i*4096);
}

static void dummy_entry(void *arg){ (void)arg; }

static void reset(void){
    scheduler_host_reset();
    mock_ticks=100;
    for(unsigned i=0;i<CPU_MAX_COUNT;i++){
        executing_cpu=i;
        scheduler_init_cpu();
    }
}

static int make_thread(int16_t affinity){
    int id=scheduler_create_thread(dummy_entry,NULL,"test",1,affinity);
    assert(id>=0);
    return id;
}

static void test_dynamic_growth(void){
    reset();
    executing_cpu=0;
    enum { N = 500 };
    static int ids[N];
    for(int i=0;i<N;i++) ids[i]=make_thread(-1);
    for(int i=0;i<N;i++)
        for(int j=i+1;j<N;j++) assert(ids[i]!=ids[j]);
    assert(scheduler_thread_count()==N);
    /* Reap contract: only TERMINATED + switched-out nodes are releasable. */
    for(int i=0;i<N;i++){
        struct thread *t=scheduler_host_lookup(ids[i]);
        assert(t!=NULL);
        t->state=THREAD_TERMINATED;
        t->running_cpu=-1;
    }
    for(int i=0;i<N;i++) scheduler_free_thread_by_id(ids[i]);
    assert(scheduler_thread_count()==0);
    /* Allocator must work again after a full drain (no static slot leak). */
    int id=make_thread(-1);
    assert(scheduler_thread_count()==1);
    struct thread *last=scheduler_host_lookup(id);
    assert(last!=NULL);
    last->state=THREAD_TERMINATED;
    last->running_cpu=-1;
    scheduler_free_thread_by_id(id);
    assert(scheduler_thread_count()==0);
    scheduler_free_thread_by_id(id);
    scheduler_free_thread_by_id(-1);
    assert(scheduler_thread_count()==0);
}

static void test_oom(void){
    reset();
    executing_cpu=0;
    mock_fail_after=0;
    assert(scheduler_create_thread(dummy_entry,NULL,"oom",1,-1)<0);
    mock_fail_after=-1;
    assert(make_thread(-1)>=0);
}

static void test_selection(void){
    reset();
    executing_cpu=0;
    int id=make_thread(1);
    struct thread *t=scheduler_host_lookup(id);
    assert(t!=NULL);
    assert(pick_next()==scheduler_host_idle(0));
    executing_cpu=1;
    assert(pick_next()==t);
    t->running_cpu=0;
    assert(pick_next()==scheduler_host_idle(1));
    t->running_cpu=-1;
    t->kernel_only=true;
    assert(pick_next()==scheduler_host_idle(1));
    executing_cpu=0;
    assert(pick_next()==t); /* Service override preserves affinity. */
    assert(t->affinity==1);
    t->state=THREAD_BLOCKED;
    t->wake_tick=101;
    assert(pick_next()==scheduler_host_idle(0));
    t->wake_tick=100;
    assert(pick_next()==t);
    assert(t->wake_tick==0);
    t->state=THREAD_TERMINATED;
    t->running_cpu=1;
    scheduler_free_thread_by_id(id); /* Still owned elsewhere: must survive. */
    assert(scheduler_host_lookup(id)==t);
    assert(scheduler_thread_count()==1);
    t->running_cpu=-1;
    scheduler_free_thread_by_id(id);
    assert(scheduler_host_lookup(id)==NULL);
    assert(scheduler_thread_count()==0);
}

struct claim_ctx {
    int *index_by_id;
    int max_id;
    unsigned *claims;
    int n;
};

static struct claim_ctx *g_claims;
static void *claim_threads(void *arg){
    executing_cpu=(uint32_t)(uintptr_t)arg;
    for(;;){
        spin_lock(&runqueue_lock);
        struct thread *t=pick_next();
        if(t->idle){ spin_unlock(&runqueue_lock); break; }
        t->state=THREAD_RUNNING;
        t->running_cpu=(int16_t)executing_cpu;
        int idx=((uint32_t)t->id<(uint32_t)g_claims->max_id) ? g_claims->index_by_id[t->id] : -1;
        assert(idx>=0);
        g_claims->claims[idx]++;
        spin_unlock(&runqueue_lock);
    }
    return NULL;
}

static void test_concurrent_claims(void){
    reset();
    enum { N = 200 }; /* Well past the old static cap of 64. */
    static int ids[N];
    static int index_by_id[1<<16];
    static unsigned claims[N];
    for(int i=0;i<N;i++){
        ids[i]=make_thread(-1);
        index_by_id[ids[i]%(1<<16)]=i;
        claims[i]=0;
    }
    static struct claim_ctx ctx;
    ctx.index_by_id=index_by_id;
    ctx.max_id=1<<16;
    ctx.claims=claims;
    ctx.n=N;
    g_claims=&ctx;
    pthread_t workers[CPU_MAX_COUNT];
    for(unsigned i=0;i<CPU_MAX_COUNT;i++) assert(!pthread_create(&workers[i],NULL,claim_threads,(void*)(uintptr_t)i));
    for(unsigned i=0;i<CPU_MAX_COUNT;i++) assert(!pthread_join(workers[i],NULL));
    for(int i=0;i<N;i++) assert(claims[i]==1);
}

static void test_preemption_guard(void){
    reset();
    executing_cpu=0;
    int id=make_thread(-1);
    struct thread *t=scheduler_host_lookup(id);
    struct scheduler_cpu *cpu=&cpu_schedulers[0];
    cpu->initialized=true;
    cpu->started=true;
    cpu->last_tick=100;
    cpu->current=t;
    t->state=THREAD_RUNNING;
    t->ticks_remaining=1;

    assert(scheduler_preempt_disable());
    assert(scheduler_preempt_disable());
    mock_ticks=101;
    scheduler_on_timer_interrupt();
    assert(cpu->current==t);
    assert(cpu->preempt_depth==2);
    assert(cpu->reschedule_pending);

    scheduler_preempt_enable();
    assert(cpu->preempt_depth==1);
    assert(cpu->reschedule_pending);
    scheduler_preempt_enable();
    assert(cpu->preempt_depth==0);
    assert(!cpu->reschedule_pending);
    assert(cpu->current==t);
    assert(t->ticks_remaining==SCHEDULER_TIME_SLICE_MS);
    mock_ticks=100;
}

int main(void){
    test_dynamic_growth();
    test_oom();
    test_selection();
    test_concurrent_claims();
    test_preemption_guard();
    puts("Scheduler dynamic runqueue, affinity, ownership, wakeup and concurrent selection tests passed");
    return 0;
}
