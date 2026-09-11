#pragma once

#include <stdint.h>

void timer_init(uint32_t frequency_hz);
void timer_tick(void);
void timer_sleep(uint32_t milliseconds);
uint64_t timer_ticks(void);
uint64_t timer_idle_tsc(void);
