#pragma once
#include <stdint.h>
#include <stdbool.h>
#define SMP_RESCHEDULE_VECTOR 0xF0
#define SMP_TIMER_VECTOR 0xF1
#define SMP_SPURIOUS_VECTOR 0xFF
bool lapic_init(void);
bool lapic_init_cpu(void);
void lapic_start_timer(void);
void lapic_eoi(void);
bool lapic_send(uint32_t logical_id, uint32_t command);
