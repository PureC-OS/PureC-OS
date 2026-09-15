#pragma once

#include <stdbool.h>
#include <stdint.h>

#define LAPIC_TIMER_VECTOR 0xE0
#define LAPIC_SPURIOUS_VECTOR 0xFF

bool apic_init(void);
bool apic_present(void);
void apic_disable(void);
void apic_test_tick(void);
uint64_t apic_test_ticks(void);
bool apic_wait_tick(uint32_t timeout_ms);
void apic_cpu_enable(void);
void apic_eoi(void);
uint32_t apic_lapic_id(void);
uint32_t apic_raw_lapic_id(void);
void apic_mask_pit_timer(void);
