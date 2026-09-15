#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct thread;

#define SMP_MAX_CPUS 16

struct cpu_local {
    struct thread *current;
    uint32_t present;
    uint32_t online;
    uint32_t lapic_id;
    uint32_t index;
    struct thread *idle;
    uint64_t timer_ticks;
};

struct ap_boot_info {
    uint64_t stack_top;
    uint64_t kernel_cr3;
    uint64_t cpu_index;
};

void smp_early_bsp(void);
void smp_init(void);
uint32_t smp_online_count(void);
uint32_t smp_cpu_index(void);
struct cpu_local *smp_this(void);
struct cpu_local *smp_cpu(uint32_t index);
void smp_gs_set(uint32_t index);
void ap_main(void *boot_arg);
