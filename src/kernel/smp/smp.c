#include "smp.h"
#include "cpu.h"
#include "lapic.h"
#include "../sync/spinlock.h"
#include "../diagnostics/klog.h"
#include "../diagnostics/panic.h"
#include "../../drivers/interrupts/timer.h"
#include "../process/scheduler.h"
#include "../../boot/limine.h"
#include "../../arch/x86_64/gdt/include/gdt.h"
#include "../../arch/x86_64/idt/include/idt.h"
#include "../../arch/x86_64/fpu/include/fpu.h"
#include "../../mm/vmm.h"

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
static uint64_t tlb_request[CPU_MAX_COUNT];
static uint64_t tlb_ack[CPU_MAX_COUNT];
static spinlock_t tlb_lock = SPINLOCK_INIT;
static uint64_t tlb_generation;

extern void smp_ap_entry(struct limine_smp_info *info);

static uint64_t read_tsc(void){
    uint32_t low, high;
    __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
    return ((uint64_t)high << 32) | low;
}

static uint64_t tsc_frequency(void){
    uint32_t maximum, eax, ebx, ecx, edx;
    __asm__ volatile("cpuid"
                     : "=a"(maximum), "=b"(ebx), "=c"(ecx), "=d"(edx)
                     : "a"(0), "c"(0));
    if(maximum >= 0x15){
        __asm__ volatile("cpuid"
                         : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                         : "a"(0x15), "c"(0));
        if(eax && ebx && ecx) return ((uint64_t)ecx * ebx) / eax;
    }
    if(maximum >= 0x16){
        __asm__ volatile("cpuid"
                         : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                         : "a"(0x16), "c"(0));
        if(eax) return (uint64_t)eax * 1000000ULL;
    }
    return 3000000000ULL;
}

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
        for(;;) __asm__ volatile("cli; hlt");
    }

    uint32_t id = context->logical_id;
    if(!vmm_init_cpu()
       || !gdt_init_cpu(id, context->stack_top)){
        cpu_fail(id);
        for(;;) __asm__ volatile("cli; hlt");
    }
    if(gdt_current_cpu_id() != id || scheduler_current_thread() != NULL){
        cpu_fail(id);
        for(;;) __asm__ volatile("cli; hlt");
    }
    idt_init_cpu();
    if(!fpu_init_cpu()){
        cpu_fail(id);
        for(;;) __asm__ volatile("cli; hlt");
    }

    if(!lapic_init_cpu()){
        cpu_fail(id);
        for(;;) __asm__ volatile("cli; hlt");
    }
    scheduler_init_cpu();
    if(!cpu_publish_idle(id)){
        for(;;) __asm__ volatile("cli; hlt");
    }

    while(!__atomic_load_n(&scheduler_released, __ATOMIC_ACQUIRE))
        __asm__ volatile("sti; hlt; cli" ::: "memory");
    lapic_start_timer();
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

    __atomic_store_n(&limine_info->extra_argument,
                     (uint64_t)(uintptr_t)context, __ATOMIC_RELEASE);
    __atomic_store_n(&limine_info->goto_address, smp_ap_entry,
                     __ATOMIC_RELEASE);

    uint64_t frequency = tsc_frequency();
    uint64_t duration = (frequency / 1000) * (timeout_ms ? timeout_ms : 1);
    uint64_t start = read_tsc();
    while(read_tsc() - start < duration){
        enum cpu_state state = cpu_get_state(logical_id);
        if(state == CPU_IDLE) return true;
        if(state == CPU_FAILED) return false;
        __asm__ volatile("pause" ::: "memory");
    }
    if(!cpu_timeout_start(logical_id))
        return cpu_get_state(logical_id) == CPU_IDLE;
    return false;
}


void smp_start_all(struct limine_smp_response *response){
    if(!lapic_init()){
        klog(KLOG_WARN, "smp: LAPIC unavailable; BSP scheduling only");
        return;
    }
    for(uint32_t id=1; id<cpu_registered_count(); id++){
        bool ok=smp_start_cpu(response,id,1000);
        klogf(ok ? KLOG_OK : KLOG_WARN,
              "smp: cpu%u lapic=%u %s state=%u",id,cpu_get_info(id)->lapic_id,
              ok ? "ready (HLT)" : "startup failed",(uint32_t)cpu_get_state(id));
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

static void flush_local_tlb(void){
    /* Include global translations, and work independently of current CR3.
       This kernel does not enable PCID. No allocation or locks in NMI. */
    uint64_t cr4,cr3;
    __asm__ volatile("mov %%cr4,%0; mov %%cr3,%1":"=r"(cr4),"=r"(cr3));
    if(cr4&(1ULL<<7)){
        __asm__ volatile("mov %0,%%cr4; mov %1,%%cr4"::"r"(cr4&~(1ULL<<7)),"r"(cr4):"memory");
    } else __asm__ volatile("mov %0,%%cr3"::"r"(cr3):"memory");
}
bool smp_handle_nmi(void){
    uint32_t id=gdt_current_cpu_id();
    if(id>=CPU_MAX_COUNT) return false;
    if(__atomic_load_n(&stop_mask,__ATOMIC_ACQUIRE)&(1U<<id)){
        for(;;) __asm__ volatile("cli; hlt");
    }
    uint64_t request=__atomic_load_n(&tlb_request[id],__ATOMIC_ACQUIRE);
    if(request!=__atomic_load_n(&tlb_ack[id],__ATOMIC_RELAXED)){
        flush_local_tlb();
        __atomic_store_n(&tlb_ack[id],request,__ATOMIC_RELEASE);
    }
    /* A delayed/coalesced shootdown NMI is harmless. */
    return request!=0;
}
void smp_tlb_shootdown(void){
    uint64_t flags=spin_lock_irqsave(&tlb_lock);
    uint64_t generation=++tlb_generation;
    uint32_t self=gdt_current_cpu_id(), targets=0;
    flush_local_tlb();
    for(uint32_t id=0;id<cpu_registered_count();id++){
        if(id==self || !cpu_is_online(id)) continue;
        targets|=1U<<id;
        __atomic_store_n(&tlb_request[id],generation,__ATOMIC_RELEASE);
        if(!lapic_send(id,4U<<8)) kernel_panic("smp: TLB IPI send failed");
    }
    uint64_t start=timer_ticks();
    for(uint32_t id=0;id<cpu_registered_count();id++){
        if(!(targets&(1U<<id))) continue;
        while(__atomic_load_n(&tlb_ack[id],__ATOMIC_ACQUIRE)!=generation){
            if(timer_ticks()-start>1000) kernel_panic("smp: TLB acknowledgement timeout");
            __asm__ volatile("pause");
        }
    }
    spin_unlock_irqrestore(&tlb_lock,flags);
}
void smp_stop_others(void){
    uint32_t self=gdt_current_cpu_id(), mask=0;
    for(uint32_t id=0;id<cpu_registered_count();id++)
        if(id!=self && cpu_is_online(id)) mask|=1U<<id;
    __atomic_fetch_or(&stop_mask,mask,__ATOMIC_RELEASE);
    for(uint32_t id=0;id<cpu_registered_count();id++)
        if(mask&(1U<<id)) (void)lapic_send(id,4U<<8);
}
