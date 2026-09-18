#include "cpu.h"
#include "smp.h"
#include "../process/scheduler.h"
#include "../process/process.h"
#include "../sync/spinlock.h"
#include "../diagnostics/klog.h"
#include "../diagnostics/panic.h"
#include "../../arch/x86_64/gdt/include/gdt.h"
#include "../../drivers/interrupts/timer.h"
#include "../../mm/pmm.h"
#include "../../mm/vmm.h"

/* Finite boot diagnostic: each worker has an explicit placement, then moves
   to another CPU. All logging is performed by the BSP coordinator. */
struct result { uint32_t ready, done, failed, seen; uint64_t switches; };
static struct result results[CPU_MAX_COUNT];
static uint32_t participants;
static bool go;
extern const unsigned char smp_user_elf_start[], smp_user_elf_end[];
static uint64_t shared_counter;
static uint32_t peer_progress[CPU_MAX_COUNT];

static void worker(void *argument){
    uint32_t slot=(uint32_t)(uintptr_t)argument;
    struct result *r=&results[slot];
    __atomic_store_n(&r->ready,1,__ATOMIC_RELEASE);
    while(!__atomic_load_n(&go,__ATOMIC_ACQUIRE)) scheduler_sleep(1);
    uint64_t marker=0x5a5a123400000000ULL|slot;
    __asm__ volatile("movq %0,%%xmm15"::"r"(marker):"memory");
    uint32_t peer=__atomic_load_n(&peer_progress[slot],__ATOMIC_ACQUIRE);
    uint64_t start=timer_ticks();
    /* No yield/sleep here: only the timer can schedule the peer on this CPU. */
    while(__atomic_load_n(&peer_progress[slot],__ATOMIC_ACQUIRE)==peer){
        if(timer_ticks()-start>250){ r->failed=1; break; }
        __asm__ volatile("pause");
    }
    uint64_t restored;
    __asm__ volatile("movq %%xmm15,%0":"=r"(restored));
    if(restored!=marker) r->failed=1;
    for(unsigned n=0;n<8;n++){
        uint32_t id=gdt_current_cpu_id();
        r->seen|=1U<<id;
        uint64_t frame=pmm_allocate_page();
        uint64_t space=vmm_create_address_space();
        if(!frame || !space){ r->failed=1; break; }
        uint64_t *page=pmm_physical_to_virtual(frame);
        *page=marker+n;
        if(!vmm_map_page(space,0x400000,frame,VMM_PAGE_USER|VMM_PAGE_WRITABLE)
           || vmm_translate(space,0x400000)!=frame || *page!=marker+n) r->failed=1;
        vmm_destroy_address_space(space); /* also frees frame; includes shootdown */
        __atomic_fetch_add(&shared_counter,1,__ATOMIC_RELAXED);
        scheduler_sleep(1);
    }
    uint32_t target=slot;
    for(unsigned n=1;n<=CPU_MAX_COUNT;n++){
        uint32_t candidate=(slot+n)%CPU_MAX_COUNT;
        if(participants&(1U<<candidate)){ target=candidate; break; }
    }
    scheduler_set_affinity(scheduler_current_tid(),(int16_t)target);
    scheduler_yield();
    if(gdt_current_cpu_id()!=target) r->failed=1;
    r->seen|=1U<<gdt_current_cpu_id();
    __asm__ volatile("movq %%xmm15,%0":"=r"(restored));
    if(restored!=marker) r->failed=1;
    __atomic_store_n(&r->done,1,__ATOMIC_RELEASE);
}

static void preemption_peer(void *argument){
    uint32_t id=(uint32_t)(uintptr_t)argument;
    while(!__atomic_load_n(&results[id].done,__ATOMIC_ACQUIRE)){
        __atomic_fetch_add(&peer_progress[id],1,__ATOMIC_RELEASE);
        scheduler_sleep(1);
    }
}

static void coordinator(void *unused){
    (void)unused;
    uint64_t start=timer_ticks();
    for(;;){
        uint32_t ready=0;
        for(unsigned id=0;id<CPU_MAX_COUNT;id++)
            if(__atomic_load_n(&results[id].ready,__ATOMIC_ACQUIRE)) ready|=1U<<id;
        if((ready&participants)==participants) break;
        if(timer_ticks()-start>5000) kernel_panic("smp selftest: workers did not start");
        scheduler_sleep(1);
    }
    __atomic_store_n(&go,true,__ATOMIC_RELEASE);
    start=timer_ticks();
    for(;;){
        uint32_t done=0;
        for(unsigned id=0;id<CPU_MAX_COUNT;id++)
            if(__atomic_load_n(&results[id].done,__ATOMIC_ACQUIRE)) done|=1U<<id;
        if((done&participants)==participants) break;
        if(timer_ticks()-start>10000) kernel_panic("smp selftest: progress timeout");
        scheduler_sleep(5);
    }
    for(unsigned id=0;id<CPU_MAX_COUNT;id++){
        if(!(participants&(1U<<id))) continue;
        klogf(KLOG_INFO,"smp-test: worker=%u CPUs=0x%x preemption/memory/FPU/migration=%s",id,results[id].seen,
              results[id].failed ? "FAIL" : "PASS");
        if(results[id].failed) kernel_panic("smp selftest failed");
    }
    int32_t pids[2];
    unsigned targets[2]={0,0};
    for(unsigned id=1;id<CPU_MAX_COUNT;id++){
        if(!(participants&(1U<<id))) continue;
        if(!targets[0]) targets[0]=id;
        else { targets[1]=id; break; }
    }
    for(unsigned i=0;i<2;i++){
        uint64_t flags=irq_save();
        pids[i]=process_spawn_elf(smp_user_elf_start,
            (uint64_t)(smp_user_elf_end-smp_user_elf_start),"smp-ring3","");
        if(pids[i]<0 || !process_set_affinity((uint32_t)pids[i],(int16_t)targets[i]))
            kernel_panic("smp selftest: cannot create Ring-3 test");
        irq_restore(flags);
    }
    for(unsigned i=0;i<2;i++){
        int32_t status=-1;
        if(process_wait((uint32_t)pids[i],&status,false)!=pids[i] || status!=0)
            kernel_panic("smp selftest: Ring-3 affinity/syscall/FPU failed");
        klogf(KLOG_OK,"smp-test: Ring-3 CPU=%u syscall/FPU/reap PASS",targets[i]);
    }
    klogf(KLOG_OK,"smp-test: PASS participants=0x%x allocations=%llu",participants,shared_counter);
}

void smp_selftest_start(void){
    for(unsigned id=0;id<cpu_registered_count();id++){
        if(!cpu_is_online(id)) continue;
        participants|=1U<<id;
        if(scheduler_create_thread(worker,(void*)(uintptr_t)id,"smp-test",2,(int16_t)id)<0)
            kernel_panic("smp selftest: cannot create worker");
        if(scheduler_create_thread(preemption_peer,(void*)(uintptr_t)id,"smp-peer",2,(int16_t)id)<0)
            kernel_panic("smp selftest: cannot create peer");
    }
    if(scheduler_create_thread(coordinator,NULL,"smp-check",2,0)<0)
        kernel_panic("smp selftest: cannot create coordinator");
}
