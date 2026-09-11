#pragma once

#include "../../boot/limine.h"
#include <stdint.h>

void system_info_init(const struct limine_memmap_response *memory_map);
const char *system_info_cpu_name(void);
uint64_t system_info_usable_ram_bytes(void);
uint64_t system_info_total_ram_bytes(void);
uint64_t system_info_tsc_frequency_hz(void);
uint32_t system_info_logical_processors(void);
uint64_t system_info_uptime_ms(void);
uint32_t system_info_cpu_usage_percent(void);
