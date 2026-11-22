#include <arch/arch.h>
#include <kernel/smp/smp.h>
#include <kernel/smp/cpu.h>
#include <arch/x86_64/lapic.h>
#include <kernel/smp/percpu.h>
#include <arch/x86_64/cpufreq.h>
#include <kernel/sync/spinlock.h>
#include <kernel/diagnostics/klog.h>
#include <kernel/diagnostics/panic.h>
#include <drivers/interrupts/timer.h>
#include <kernel/process/scheduler.h>
#include <boot/limine.h>
#include <arch/x86_64/gdt/include/gdt.h>
#include <arch/x86_64/idt/include/idt.h>
#include <arch/x86_64/fpu/include/fpu.h>
#include <mm/vmm.h>
#include <arch/x86_64/pat.h>

#include <stddef.h>

#define AP_STACK_SIZE 32768

struct ap_boot_context {
    uint64_t stack_top;
    uint32_t logical_id;
    uint32_t lapic_id;
};

static struct ap_boot_context ap_contexts[CPU_MAX_COUNT];
static uint8_t ap_stacks[CPU_MAX_COUNT][AP_STACK_SIZE]
    __attribute__((aligned(16)));

_Static_assert(offsetof(struct ap_boot_context, stack_top) == 0,
               "AP entry stack offset changed");
_Static_assert(offsetof(struct limine_smp_info, extra_argument) == 24,
               "Limine SMP ABI changed");

static bool scheduler_released;
static uint32_t stop_mask;
static uint32_t stopped_mask;
static uint64_t tlb_request[CPU_MAX_COUNT];
static uint64_t tlb_ack[CPU_MAX_COUNT];
static spinlock_t tlb_lock = SPINLOCK_INIT;
static uint64_t tlb_generation;


#define MSR_TSC_ADJUST 0x3B
#define MSR_TSC 0x10
#define TSC_SYNC_ROUNDS 32U
#define TSC_SYNC_DONE 0xFFFFFFFFU

static bool tsc_adjust_supported;
static uint64_t bsp_tsc_adjust;
static volatile uint32_t tsc_sync_req;
static volatile uint32_t tsc_sync_ack;
static volatile uint64_t tsc_sync_bsp_value;
static uint32_t tsc_sync_last;
static int64_t tsc_sync_before[CPU_MAX_COUNT];
static int64_t tsc_sync_after[CPU_MAX_COUNT];
static bool tsc_sync_adjust_fixed[CPU_MAX_COUNT];

static inline uint64_t ordered_tsc(void){
    __asm__ volatile("lfence" ::: "memory");
    uint64_t value = arch_read_timestamp();
    __asm__ volatile("lfence" ::: "memory");
    return value;
}

static void tsc_sync_init_bsp(void){
    uint32_t ebx = 0;
    arch_cpuid(7, 0, NULL, &ebx, NULL, NULL);
    tsc_adjust_supported = (ebx & (1U << 1)) != 0;
    if(tsc_adjust_supported) bsp_tsc_adjust = arch_rdmsr(MSR_TSC_ADJUST);
    klogf(KLOG_INFO, "tsc: sync TSC_ADJUST %s, bsp adjust %lld",
          tsc_adjust_supported ? "supported" : "absent", (long long)bsp_tsc_adjust);
}

static void tsc_sync_bsp_serve(void){
    uint32_t round = __atomic_load_n(&tsc_sync_req, __ATOMIC_ACQUIRE);
    if(!round || round == TSC_SYNC_DONE || round == tsc_sync_last) return;
    tsc_sync_bsp_value = ordered_tsc();
    tsc_sync_last = round;
    __atomic_store_n(&tsc_sync_ack, round, __ATOMIC_RELEASE);
}

static int64_t tsc_sync_measure(uint32_t *round){
    int64_t best_offset = 0;
    uint64_t best_rtt = UINT64_MAX;
    for(uint32_t i = 0; i < TSC_SYNC_ROUNDS; i++){
        uint32_t r = ++(*round);
        uint64_t t0 = ordered_tsc();
        __atomic_store_n(&tsc_sync_req, r, __ATOMIC_RELEASE);
        while(__atomic_load_n(&tsc_sync_ack, __ATOMIC_ACQUIRE) != r)
            arch_cpu_relax();
        uint64_t t1 = ordered_tsc();
        uint64_t bsp = tsc_sync_bsp_value;
        uint64_t rtt = t1 - t0;
        if(rtt < best_rtt){
            best_rtt = rtt;
            best_offset = (int64_t)(bsp - (t0 + rtt / 2));
        }
    }
    return best_offset;
}

static void tsc_sync_ap(uint32_t id){
    if(tsc_adjust_supported){
        uint64_t own = arch_rdmsr(MSR_TSC_ADJUST);
        if(own != bsp_tsc_adjust){
            arch_wrmsr(MSR_TSC_ADJUST, bsp_tsc_adjust);
            tsc_sync_adjust_fixed[id] = true;
        }
    }
    uint32_t round = 0;
    int64_t offset = tsc_sync_measure(&round);
    tsc_sync_before[id] = offset;
    if(offset > 2000 || offset < -2000){
        if(tsc_adjust_supported)
            arch_wrmsr(MSR_TSC_ADJUST, arch_rdmsr(MSR_TSC_ADJUST) + (uint64_t)offset);
        else
            arch_wrmsr(MSR_TSC, ordered_tsc() + (uint64_t)offset);
    }
    tsc_sync_after[id] = tsc_sync_measure(&round);
    __atomic_store_n(&tsc_sync_req, TSC_SYNC_DONE, __ATOMIC_RELEASE);
}

extern void smp_ap_entry(struct limine_smp_info *info);
static struct limine_smp_info *find_limine_cpu(
    struct limine_smp_response *response, const struct cpu_info *cpu){
    if(!response || !response->cpus || !cpu || !cpu->ids_valid) return NULL;
    for(uint64_t i = 0; i < response->cpu_count; i++){
        struct limine_smp_info *info = response->cpus[i];
        if(info && info->lapic_id == cpu->lapic_id
           && info->processor_id == cpu->processor_id) return info;
    }
    return NULL;
}

__attribute__((noreturn))
void smp_ap_main(struct ap_boot_context *context,
                 struct limine_smp_info *limine_info){
    __asm__ volatile("cli; cld" ::: "memory");
    if(!context || !limine_info
       || context->logical_id == 0
       || context->logical_id >= CPU_MAX_COUNT
       || context->lapic_id != limine_info->lapic_id){
        arch_cpu_stop();
    }

    uint32_t id = context->logical_id;
    if(!percpu_install(id)){
        arch_cpu_stop();
    }
    if(!vmm_init_cpu()
       || !gdt_init_cpu(id, context->stack_top)){
        cpu_fail(id);
        arch_cpu_stop();
    }
    if(gdt_current_cpu_id() != id || scheduler_current_thread() != NULL){
        cpu_fail(id);
        arch_cpu_stop();
    }
    idt_init_cpu();
    if(!fpu_init_cpu()){
        cpu_fail(id);
        arch_cpu_stop();
    }

    if(!lapic_init_cpu()){
        cpu_fail(id);
        arch_cpu_stop();
    }
    arch_pat_init_ap();
    tsc_sync_ap(id);
    cpufreq_init_cpu();
    scheduler_init_cpu();
    if(!cpu_publish_idle(id)){
        arch_cpu_stop();
    }

    while(!__atomic_load_n(&scheduler_released, __ATOMIC_ACQUIRE)){
        arch_cpu_idle();
        arch_irq_disable();
    }
    (void)lapic_start_timer();
    scheduler_start();
    scheduler_idle_loop();
}

bool smp_start_cpu(struct limine_smp_response *response,
                   uint32_t logical_id, uint32_t timeout_ms){
    const struct cpu_info *cpu = cpu_get_info(logical_id);
    struct limine_smp_info *limine_info = find_limine_cpu(response, cpu);
    if(!cpu || cpu->is_bsp || !limine_info
       || __atomic_load_n(&limine_info->goto_address, __ATOMIC_ACQUIRE))
        return false;
    if(!cpu_try_start(logical_id)) return false;

    struct ap_boot_context *context = &ap_contexts[logical_id];
    context->stack_top = (uint64_t)(uintptr_t)
        &ap_stacks[logical_id][AP_STACK_SIZE];
    context->logical_id = logical_id;
    context->lapic_id = cpu->lapic_id;
    tsc_sync_req = 0;
    tsc_sync_ack = 0;
    tsc_sync_last = 0;

    __atomic_store_n(&limine_info->extra_argument,
                     (uint64_t)(uintptr_t)context, __ATOMIC_RELEASE);
    __atomic_store_n(&limine_info->goto_address, smp_ap_entry,
                     __ATOMIC_RELEASE);

    uint64_t frequency = arch_cpuid_tsc_hz();
    if(!frequency) frequency = 3000000000ULL;
    uint64_t duration = (frequency / 1000) * (timeout_ms ? timeout_ms : 1);
    uint64_t start = arch_read_timestamp();
    while(arch_read_timestamp() - start < duration){
        tsc_sync_bsp_serve();
        enum cpu_state state = cpu_get_state(logical_id);
        if(state == CPU_IDLE) return true;
        if(state == CPU_FAILED) return false;
        arch_cpu_relax();
    }
    if(!cpu_timeout_start(logical_id))
        return cpu_get_state(logical_id) == CPU_IDLE;
    return false;
}


void smp_start_all(struct limine_smp_response *response){
    arch_pat_init_bsp();
    cpufreq_init_bsp();
    if(!lapic_init()){
        klog(KLOG_WARN, "smp: LAPIC unavailable; BSP scheduling only");
        return;
    }
    tsc_sync_init_bsp();
    for(uint32_t id=1;id<cpu_registered_count();id++){
        bool ok=smp_start_cpu(response,id,1000);
        klogf(ok ? KLOG_OK : KLOG_WARN,
              "smp: cpu%u lapic=%u %s state=%u",id,cpu_get_info(id)->lapic_id,
              ok ? "ready (HLT)" : "startup failed",(uint32_t)cpu_get_state(id));
        if(ok)
            klogf(KLOG_INFO, "tsc: cpu%u offset %lld -> %lld cycles%s", id,
                  (long long)tsc_sync_before[id], (long long)tsc_sync_after[id],
                  tsc_sync_adjust_fixed[id] ? " (TSC_ADJUST reset to bsp)" : "");
    }
}
void smp_release_scheduler(void){
    __atomic_store_n(&scheduler_released,true,__ATOMIC_RELEASE);
    for(uint32_t id=1;id<cpu_registered_count();id++)
        if(cpu_is_online(id)) (void)lapic_send(id,SMP_RESCHEDULE_VECTOR);
}
void smp_reschedule_cpu(uint32_t id){
    if(id!=gdt_current_cpu_id() && (scheduler_active_mask()&(1U<<id)))
        (void)lapic_send(id,SMP_RESCHEDULE_VECTOR);
}
void smp_reschedule_all(void){
    for(uint32_t id=0;id<cpu_registered_count();id++) smp_reschedule_cpu(id);
}

static volatile uint64_t cpu_cr3[CPU_MAX_COUNT];
static volatile uint8_t tlb_full[CPU_MAX_COUNT];

void smp_note_cr3(uint64_t cr3){
    uint32_t id=gdt_current_cpu_id();
    if(id>=CPU_MAX_COUNT) return;
    __atomic_store_n(&cpu_cr3[id],cr3,__ATOMIC_SEQ_CST);
}

static void flush_local_user_tlb(void){
    arch_tlb_flush_local_user();
}

static void flush_local_tlb(void){
    arch_tlb_flush_local_global();
}
bool smp_handle_nmi(void){
    uint32_t id=gdt_current_cpu_id();
    if(id>=CPU_MAX_COUNT) return false;
    if(__atomic_load_n(&stop_mask,__ATOMIC_ACQUIRE)&(1U<<id)){
        __atomic_fetch_or(&stopped_mask,1U<<id,__ATOMIC_RELEASE);
        arch_cpu_stop();
    }
    uint64_t request=__atomic_load_n(&tlb_request[id],__ATOMIC_ACQUIRE);
    if(request!=__atomic_load_n(&tlb_ack[id],__ATOMIC_RELAXED)){
        if(__atomic_exchange_n(&tlb_full[id],0,__ATOMIC_ACQ_REL)) flush_local_tlb();
        else flush_local_user_tlb();
        __atomic_store_n(&tlb_ack[id],request,__ATOMIC_RELEASE);
    }
    /* A delayed/coalesced shootdown NMI is harmless. */
    return request!=0;
}
static void tlb_shootdown_targets(uint64_t space){
    uint64_t flags=spin_lock_irqsave(&tlb_lock);
    uint64_t generation=++tlb_generation;
    uint32_t self=gdt_current_cpu_id(), targets=0;
    bool full=space==0;
    if(full) flush_local_tlb();
    else if(self>=CPU_MAX_COUNT || __atomic_load_n(&cpu_cr3[self],__ATOMIC_SEQ_CST)==space ||
            !__atomic_load_n(&cpu_cr3[self],__ATOMIC_SEQ_CST)) flush_local_user_tlb();
    for(uint32_t id=0;id<cpu_registered_count();id++){
        if(id==self || !cpu_is_online(id)) continue;
        if(!full){
            uint64_t cr3=__atomic_load_n(&cpu_cr3[id],__ATOMIC_SEQ_CST);
            if(cr3 && cr3!=space) continue;
        } else {
            __atomic_store_n(&tlb_full[id],1,__ATOMIC_RELEASE);
        }
        targets|=1U<<id;
        __atomic_store_n(&tlb_request[id],generation,__ATOMIC_RELEASE);
        if(!lapic_send(id,4U<<8)) kernel_panic("smp: TLB IPI send failed");
    }
    uint64_t start=timer_ticks();
    for(uint32_t id=0;id<cpu_registered_count();id++){
        if(!(targets&(1U<<id))) continue;
        while(__atomic_load_n(&tlb_ack[id],__ATOMIC_ACQUIRE)!=generation){
            if(timer_ticks()-start>1000) kernel_panic("smp: TLB acknowledgement timeout");
            arch_cpu_relax();
        }
    }
    spin_unlock_irqrestore(&tlb_lock,flags);
}

void smp_tlb_shootdown(void){ tlb_shootdown_targets(0); }

void smp_tlb_shootdown_space(uint64_t space){ tlb_shootdown_targets(space); }
void smp_stop_others(void){
    uint32_t self=gdt_current_cpu_id(), mask=0;
    for(uint32_t id=0;id<cpu_registered_count();id++)
        if(id!=self && cpu_is_online(id)) mask|=1U<<id;
    __atomic_fetch_or(&stop_mask,mask,__ATOMIC_RELEASE);
    for(uint32_t id=0;id<cpu_registered_count();id++)
        if(mask&(1U<<id)) (void)lapic_send(id,4U<<8);
    uint64_t start=timer_ticks();
    while((__atomic_load_n(&stopped_mask,__ATOMIC_ACQUIRE)&mask)!=mask){
        if(timer_ticks()-start>100) break;
        arch_cpu_relax();
    }
}
