#pragma once

#include <stdbool.h>
#include <stdint.h>

#define WALLCLOCK_SOURCE_NONE 0
#define WALLCLOCK_SOURCE_RTC 1
#define WALLCLOCK_SOURCE_SET 2
#define WALLCLOCK_SOURCE_NTP 3
#define WALLCLOCK_SLEW_MS_PER_UNIT 2000U
#define WALLCLOCK_SLEW_LIMIT_MS 600000LL
#define WALLCLOCK_STEP_LIMIT_MS (100LL * 365 * 86400 * 1000)
#define WALLCLOCK_TZ_LIMIT 1440
#define WALLCLOCK_ZONE_PATH "/etc/timezone"

void wallclock_init(void);
bool wallclock_valid(void);
uint32_t wallclock_source(void);
int64_t wallclock_unix_ms(void);
uint32_t wallclock_unix_seconds(void);
int32_t wallclock_tz_minutes(void);
int32_t wallclock_set(int64_t unix_seconds, int32_t tz_minutes);
const char *wallclock_zone_name(void);
bool wallclock_set_zone(const char *name);
int32_t wallclock_set_utc(int64_t unix_seconds);
void wallclock_load_zone(void);
int32_t wallclock_step_ms(int64_t delta_ms);
int32_t wallclock_slew_ms(int64_t delta_ms, int64_t *previous);
int64_t wallclock_slew_remaining_ms(void);
bool wallclock_store_rtc(void);
