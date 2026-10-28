#pragma once
#include <stdbool.h>
#include <stdint.h>

enum cpufreq_mode {
  CPUFREQ_NONE = 0,
  CPUFREQ_HWP,
  CPUFREQ_EIST,
};

void cpufreq_init_bsp(void);
void cpufreq_init_cpu(void);
enum cpufreq_mode cpufreq_get_mode(void);
uint32_t cpufreq_measure_mhz(void);
