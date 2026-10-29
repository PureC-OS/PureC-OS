#pragma once

#include <stdbool.h>
#include <stdint.h>

struct civil_time {
  int32_t year;
  uint8_t month;
  uint8_t day;
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
  uint8_t weekday;
  uint16_t yearday;
};

bool civil_valid(int32_t year, uint32_t month, uint32_t day, uint32_t hour,
                 uint32_t minute, uint32_t second);
int64_t civil_to_unix(int32_t year, uint32_t month, uint32_t day,
                      uint32_t hour, uint32_t minute, uint32_t second);
void unix_to_civil(int64_t seconds, struct civil_time *out);
