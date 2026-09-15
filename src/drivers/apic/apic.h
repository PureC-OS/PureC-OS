#pragma once

#include <stdbool.h>
#include <stdint.h>

#define LAPIC_TIMER_VECTOR 0xE0
#define LAPIC_SPURIOUS_VECTOR 0xFF

bool apic_init(void);
bool apic_present(void);
void apic_cpu_enable(void);
void apic_eoi(void);
uint32_t apic_lapic_id(void);
void apic_mask_pit_timer(void);
