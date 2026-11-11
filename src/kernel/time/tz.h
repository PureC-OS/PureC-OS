#pragma once

#include <stdbool.h>
#include <stdint.h>

#define TZ_NAME_MAX 32

enum tz_rule {
  TZ_RULE_NONE = 0,
  TZ_RULE_EU,
  TZ_RULE_US,
  TZ_RULE_AU,
  TZ_RULE_NZ
};

struct tz_zone {
  const char *name;
  const char *city;
  int16_t std_minutes;
  uint8_t rule;
};

uint32_t tz_count(void);
const struct tz_zone *tz_at(uint32_t index);
int32_t tz_find(const char *name);
bool tz_is_dst(const struct tz_zone *zone, int64_t unix_seconds);
int32_t tz_offset_minutes(const struct tz_zone *zone, int64_t unix_seconds);
void tz_format_offset(int32_t minutes, char out[10]);
