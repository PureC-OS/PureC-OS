#pragma once

#include "civil.h"
#include <stdbool.h>
#include <stdint.h>

#define RTC_STATUS_B_24H 0x02
#define RTC_STATUS_B_BINARY 0x04

struct rtc_raw {
  uint8_t second;
  uint8_t minute;
  uint8_t hour;
  uint8_t day;
  uint8_t month;
  uint8_t year;
  uint8_t century;
  uint8_t status_b;
};

bool rtc_decode(const struct rtc_raw *raw, struct civil_time *out);
void rtc_encode(const struct civil_time *time, uint8_t status_b,
                struct rtc_raw *raw);
bool rtc_read(struct civil_time *out);
bool rtc_write(const struct civil_time *time);
