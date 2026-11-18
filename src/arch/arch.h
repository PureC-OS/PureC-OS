#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(__x86_64__)
#include "x86_64/include/arch_x86_64.h"
#include "x86_64/include/arch_mmu.h"
#else
#error "unsupported architecture"
#endif

void arch_cpu_name(char *name, size_t capacity);
void arch_cpu_brand_string(char brand[49]);
uint64_t arch_cpuid_tsc_hz(void);
bool arch_cpu_has_nx(void);
bool arch_local_timer_start(void);
void arch_interrupts_early_init(void);
uint32_t arch_cpu_freq_mhz(void);
