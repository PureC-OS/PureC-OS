#include "smp.h"
#include "cpu.h"
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
    idt_init_cpu();
    if(!fpu_init_cpu()){
        cpu_fail(id);
        for(;;) __asm__ volatile("cli; hlt");
    }

    __asm__ volatile("int3");
    if(!cpu_publish_idle(id)){
        for(;;) __asm__ volatile("cli; hlt");
    }

    for(;;) __asm__ volatile("cli; hlt");
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
