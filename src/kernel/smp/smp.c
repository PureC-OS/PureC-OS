#include "smp.h"
#include "arch/x86_64/gdt/include/gdt.h"
#include "arch/x86_64/idt.h"
#include "arch/x86_64/fpu.h"
#include "arch/x86_64/mmio.h"
#include "boot/limine.h"
#include "drivers/apic/apic.h"
#include "drivers/interrupts/timer.h"
#include "kernel/diagnostics/klog.h"
#include "kernel/process/scheduler.h"
#include "kernel/sync/spinlock.h"
#include "lib/string.h"
#include "mm/vmm.h"
#include <stdint.h>

#define AP_STACK_SIZE 16384
#define AP_ONLINE_TIMEOUT_MS 2000

extern struct limine_smp_response *smp_response_ptr;
extern void ap_trampoline(void);
extern bool scheduler_is_started(void);

static struct cpu_local cpus[SMP_MAX_CPUS];
static struct ap_boot_info ap_boot[SMP_MAX_CPUS];
static uint8_t ap_stacks[SMP_MAX_CPUS][AP_STACK_SIZE]
    __attribute__((aligned(16)));
static uint32_t online_count = 1;
static bool gs_active = false;

static void write_gs_base(uint64_t base){
    uint32_t low = (uint32_t)base;
    uint32_t high = (uint32_t)(base >> 32);
    __asm__ volatile("wrmsr" :: "c"(0xC0000101U), "a"(low), "d"(high)
                     : "memory");
}

struct cpu_local *smp_this(void){
    if(!gs_active) return &cpus[0];
    struct cpu_local *cpu;
    __asm__ volatile("mov %%gs:0,%0" : "=r"(cpu));
    return cpu;
}

struct cpu_local *smp_cpu(uint32_t index){
    if(index >= SMP_MAX_CPUS) return NULL;
    return &cpus[index];
}

uint32_t smp_online_count(void){
    return online_count;
}

uint32_t smp_cpu_index(void){
    return smp_this()->index;
}

void smp_gs_set(uint32_t index){
    if(index >= SMP_MAX_CPUS) index = 0;
    write_gs_base((uint64_t)(uintptr_t)&cpus[index]);
    gs_active = true;
}

void smp_early_bsp(void){
    memset(cpus, 0, sizeof(cpus));
    cpus[0].present = 1;
    cpus[0].online = 1;
    cpus[0].index = 0;
    smp_gs_set(0);
}

void ap_main(void *boot_arg){
    struct ap_boot_info *boot = boot_arg;
    uint32_t index = (uint32_t)boot->cpu_index;
    gdt_install_cpu(index);
    idt_install_cpu();
    fpu_enable_cpu();
    smp_gs_set(index);
    cpus[index].lapic_id = apic_lapic_id();
    apic_cpu_enable();
    __asm__ volatile("sti" ::: "memory");
    cpus[index].online = 1;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    while(!scheduler_is_started())
        __asm__ volatile("pause");
    scheduler_enter();
    for(;;) __asm__ volatile("cli; hlt");
}

void smp_init(void){
    if(!smp_response_ptr || !smp_response_ptr->cpu_count){
        klog(KLOG_WARN, "smp: no Limine SMP response, single CPU");
        return;
    }
    if(!apic_init()){
        klog(KLOG_WARN, "smp: APIC unavailable, single CPU");
        return;
    }
    cpus[0].lapic_id = apic_lapic_id();
    uint64_t detected = smp_response_ptr->cpu_count;
    if(detected > SMP_MAX_CPUS) detected = SMP_MAX_CPUS;
    uint64_t kernel_cr3 = vmm_kernel_address_space();
    uint32_t index = 1;
    for(uint64_t i = 0; i < smp_response_ptr->cpu_count
        && index < SMP_MAX_CPUS; i++){
        struct limine_smp_info *info = smp_response_ptr->cpus[i];
        if(!info) continue;
        if(info->lapic_id == cpus[0].lapic_id) continue;
        cpus[index].present = 1;
        cpus[index].index = index;
        cpus[index].lapic_id = info->lapic_id;
        ap_boot[index].stack_top =
            (uint64_t)(uintptr_t)&ap_stacks[index][AP_STACK_SIZE];
        ap_boot[index].kernel_cr3 = kernel_cr3;
        ap_boot[index].cpu_index = index;
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        *(volatile uint64_t *)&info->extra_argument =
            (uint64_t)(uintptr_t)&ap_boot[index];
        *(volatile uint64_t *)&info->goto_address =
            (uint64_t)(uintptr_t)ap_trampoline;
        uint64_t deadline = timer_ticks() + AP_ONLINE_TIMEOUT_MS;
        while(!cpus[index].online && timer_ticks() < deadline)
            __asm__ volatile("pause");
        if(cpus[index].online){
            index++;
        } else {
            cpus[index].present = 0;
            klogf(KLOG_WARN, "smp: AP lapic=%u failed to start",
                  info->lapic_id);
        }
    }
    online_count = index;
    if(online_count > 1)
        apic_mask_pit_timer();
    klogf(KLOG_OK, "smp: %u/%llu CPUs online",
          online_count, (unsigned long long)detected);
}
