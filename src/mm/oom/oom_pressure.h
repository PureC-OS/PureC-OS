#pragma once
#include <stdbool.h>
#include <stdint.h>
enum oom_pressure {
    OOM_PRESSURE_OK = 0,
    OOM_PRESSURE_LOW = 1,
    OOM_PRESSURE_CRITICAL = 2
};
enum oom_pressure oom_pressure_get(void);
bool oom_pressure_user_can_alloc(uint64_t page_count);
bool oom_pressure_kernel_can_alloc(uint64_t page_count);
