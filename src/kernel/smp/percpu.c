#include "percpu.h"

#define MSR_GS_BASE 0xC0000101U
#define MSR_KERNEL_GS_BASE 0xC0000102U

static struct percpu percpu_data[CPU_MAX_COUNT];

static void wrmsr(uint32_t msr, uint64_t value)
{
    __asm__ volatile("wrmsr"
                     :
                     : "c"(msr), "a"((uint32_t)value),
                       "d"((uint32_t)(value >> 32))
                     : "memory");
}

struct percpu *percpu_of(uint32_t cpu_id)
{
    return cpu_id < CPU_MAX_COUNT ? &percpu_data[cpu_id] : NULL;
}

void percpu_set_current(uint32_t cpu_id, struct thread *thread)
{
    if (cpu_id < CPU_MAX_COUNT)
        __atomic_store_n(&percpu_data[cpu_id].current, thread,
                         __ATOMIC_RELEASE);
}

bool percpu_install(uint32_t cpu_id)
{
    if (cpu_id >= CPU_MAX_COUNT)
        return false;
    struct percpu *area = &percpu_data[cpu_id];
    area->self = area;
    area->cpu_id = cpu_id;
    wrmsr(MSR_KERNEL_GS_BASE, 0);
    wrmsr(MSR_GS_BASE, (uint64_t)(uintptr_t)area);
    return percpu_cpu_id() == cpu_id;
}
