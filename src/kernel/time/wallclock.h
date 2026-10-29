#pragma once

#include <stdbool.h>
#include <stdint.h>

#define WALLCLOCK_SOURCE_NONE 0
#define WALLCLOCK_SOURCE_RTC 1
#define WALLCLOCK_SOURCE_SET 2
#define WALLCLOCK_TZ_LIMIT 1440

void wallclock_init(void);
bool wallclock_valid(void);
uint32_t wallclock_source(void);
int64_t wallclock_unix_ms(void);
uint32_t wallclock_unix_seconds(void);
int32_t wallclock_tz_minutes(void);
int32_t wallclock_set(int64_t unix_seconds, int32_t tz_minutes);
