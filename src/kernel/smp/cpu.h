#pragma once

#include <stdbool.h>
#include <stdint.h>

#define CPU_MAX_COUNT 16

struct limine_smp_response;

enum cpu_state {
    CPU_ABSENT = 0,
    CPU_PARKED,
    CPU_STARTING,
    CPU_ONLINE,
    CPU_IDLE,
    CPU_FAILED,
    CPU_TIMED_OUT
};

struct cpu_info {
    uint32_t logical_id;
    uint32_t processor_id;
    uint32_t lapic_id;
    bool ids_valid;
    bool is_bsp;
};

void cpu_topology_init(const struct limine_smp_response *response);
uint32_t cpu_detected_count(void);
uint32_t cpu_registered_count(void);
uint32_t cpu_online_count(void);
const struct cpu_info *cpu_get_info(uint32_t logical_id);
enum cpu_state cpu_get_state(uint32_t logical_id);
bool cpu_is_online(uint32_t logical_id);
bool cpu_try_start(uint32_t logical_id);
bool cpu_publish_idle(uint32_t logical_id);
bool cpu_timeout_start(uint32_t logical_id);
void cpu_fail(uint32_t logical_id);
