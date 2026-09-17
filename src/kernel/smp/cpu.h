#pragma once

#include <stdbool.h>
#include <stdint.h>

#define CPU_MAX_COUNT 16

struct limine_smp_response;

/* Logical IDs index this table; they are not hardware APIC IDs. */
struct cpu_info {
    uint32_t logical_id;
    uint32_t processor_id;
    uint32_t lapic_id;
    bool ids_valid;
    bool is_bsp;
    bool online;
};

/* BSP-only boot initialization. Copies IDs without releasing any AP. */
void cpu_topology_init(const struct limine_smp_response *response);
uint32_t cpu_detected_count(void);
uint32_t cpu_registered_count(void);
/* CPUs executing kernel code, not the number available to the scheduler. */
uint32_t cpu_online_count(void);
/* BSP is always logical CPU 0. Returns NULL outside the registered table. */
const struct cpu_info *cpu_get_info(uint32_t logical_id);
