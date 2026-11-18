#include <arch/arch.h>
#include "percpu.h"

static struct percpu percpu_data[CPU_MAX_COUNT];

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
    arch_percpu_set_base((uint64_t)(uintptr_t)area);
    return percpu_cpu_id() == cpu_id;
}
