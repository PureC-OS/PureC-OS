#include "rtc.h"

#define CMOS_INDEX 0x70
#define CMOS_DATA 0x71
#define REG_SECOND 0x00
#define REG_MINUTE 0x02
#define REG_HOUR 0x04
#define REG_DAY 0x07
#define REG_MONTH 0x08
#define REG_YEAR 0x09
#define REG_STATUS_A 0x0A
#define REG_STATUS_B 0x0B
#define REG_CENTURY 0x32
#define STATUS_A_UPDATING 0x80
#define STATUS_B_SET 0x80
#define HOUR_PM 0x80
#define READ_ATTEMPTS 8
#define UPDATE_WAIT_LIMIT 2000000u

static uint8_t from_bcd(uint8_t value) {
  return (uint8_t)((value >> 4) * 10 + (value & 0x0F));
}

static uint8_t to_bcd(uint8_t value) {
  return (uint8_t)(((value / 10) << 4) | (value % 10));
}

static bool bcd_ok(uint8_t value) {
  return (value & 0x0F) <= 9 && (value >> 4) <= 9;
}

bool rtc_decode(const struct rtc_raw *raw, struct civil_time *out) {
  bool binary = (raw->status_b & RTC_STATUS_B_BINARY) != 0;
  bool hour24 = (raw->status_b & RTC_STATUS_B_24H) != 0;
  uint8_t hour = raw->hour;
  bool pm = false;
  if (!hour24) {
    pm = (hour & HOUR_PM) != 0;
    hour &= (uint8_t)~HOUR_PM;
  }
  uint8_t fields[7] = {raw->second, raw->minute, hour,
                       raw->day,    raw->month,  raw->year, raw->century};
  if (!binary) {
    for (int i = 0; i < 7; i++) {
      if (i == 6 && fields[i] == 0)
        continue;
      if (!bcd_ok(fields[i]))
        return false;
      fields[i] = from_bcd(fields[i]);
    }
  }
  uint32_t second = fields[0], minute = fields[1], hour_value = fields[2];
  if (!hour24) {
    if (hour_value < 1 || hour_value > 12)
      return false;
    hour_value %= 12;
    if (pm)
      hour_value += 12;
  }
  int32_t year = fields[5];
  uint8_t century = fields[6];
  if (century >= 19 && century <= 21)
    year += century * 100;
  else
    year += year < 70 ? 2000 : 1900;
  if (!civil_valid(year, fields[4], fields[3], hour_value, minute, second))
    return false;
  unix_to_civil(civil_to_unix(year, fields[4], fields[3], hour_value, minute,
                              second),
                out);
  return true;
}

void rtc_encode(const struct civil_time *time, uint8_t status_b,
                struct rtc_raw *raw) {
  bool binary = (status_b & RTC_STATUS_B_BINARY) != 0;
  bool hour24 = (status_b & RTC_STATUS_B_24H) != 0;
  uint8_t hour = time->hour;
  bool pm = false;
  if (!hour24) {
    pm = hour >= 12;
    hour = (uint8_t)(hour % 12);
    if (hour == 0)
      hour = 12;
  }
  uint8_t values[7] = {time->second, time->minute, hour,
                       time->day,    time->month,
                       (uint8_t)(time->year % 100),
                       (uint8_t)(time->year / 100)};
  if (!binary)
    for (int i = 0; i < 7; i++)
      values[i] = to_bcd(values[i]);
  if (!hour24 && pm)
    values[2] |= HOUR_PM;
  raw->second = values[0];
  raw->minute = values[1];
  raw->hour = values[2];
  raw->day = values[3];
  raw->month = values[4];
  raw->year = values[5];
  raw->century = values[6];
  raw->status_b = status_b;
}

#ifndef PUREC_HOST_TEST
static inline void port_out(uint16_t port, uint8_t value) {
  __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t port_in(uint16_t port) {
  uint8_t value;
  __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
  return value;
}

static uint8_t cmos_read(uint8_t reg) {
  port_out(CMOS_INDEX, reg);
  return port_in(CMOS_DATA);
}

static void cmos_write(uint8_t reg, uint8_t value) {
  port_out(CMOS_INDEX, reg);
  port_out(CMOS_DATA, value);
}

static bool wait_stable(void) {
  for (uint32_t i = 0; i < UPDATE_WAIT_LIMIT; i++)
    if (!(cmos_read(REG_STATUS_A) & STATUS_A_UPDATING))
      return true;
  return false;
}

static bool sample(struct rtc_raw *raw) {
  if (!wait_stable())
    return false;
  raw->second = cmos_read(REG_SECOND);
  raw->minute = cmos_read(REG_MINUTE);
  raw->hour = cmos_read(REG_HOUR);
  raw->day = cmos_read(REG_DAY);
  raw->month = cmos_read(REG_MONTH);
  raw->year = cmos_read(REG_YEAR);
  raw->century = cmos_read(REG_CENTURY);
  raw->status_b = cmos_read(REG_STATUS_B);
  return true;
}

static bool same(const struct rtc_raw *a, const struct rtc_raw *b) {
  return a->second == b->second && a->minute == b->minute &&
         a->hour == b->hour && a->day == b->day && a->month == b->month &&
         a->year == b->year && a->century == b->century;
}

bool rtc_read(struct civil_time *out) {
  struct rtc_raw first, second;
  if (!sample(&first))
    return false;
  for (int attempt = 0; attempt < READ_ATTEMPTS; attempt++) {
    if (!sample(&second))
      return false;
    if (same(&first, &second))
      return rtc_decode(&second, out);
    first = second;
  }
  return false;
}

bool rtc_write(const struct civil_time *time) {
  uint8_t status_b = cmos_read(REG_STATUS_B);
  struct rtc_raw raw;
  rtc_encode(time, status_b, &raw);
  cmos_write(REG_STATUS_B, (uint8_t)(status_b | STATUS_B_SET));
  cmos_write(REG_SECOND, raw.second);
  cmos_write(REG_MINUTE, raw.minute);
  cmos_write(REG_HOUR, raw.hour);
  cmos_write(REG_DAY, raw.day);
  cmos_write(REG_MONTH, raw.month);
  cmos_write(REG_YEAR, raw.year);
  cmos_write(REG_CENTURY, raw.century);
  cmos_write(REG_STATUS_B, status_b);
  return true;
}
#endif
