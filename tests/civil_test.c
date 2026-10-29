#define PUREC_HOST_TEST 1
#include "kernel/time/civil.h"
#include "kernel/time/rtc.h"

#include <assert.h>
#include <string.h>
#include <time.h>

static void expect_round_trip(int32_t y, uint32_t mo, uint32_t d, uint32_t h,
                              uint32_t mi, uint32_t s) {
  int64_t t = civil_to_unix(y, mo, d, h, mi, s);
  struct civil_time c;
  unix_to_civil(t, &c);
  assert(c.year == y && c.month == mo && c.day == d);
  assert(c.hour == h && c.minute == mi && c.second == s);
  time_t host = (time_t)t;
  struct tm tm;
  gmtime_r(&host, &tm);
  assert(tm.tm_year + 1900 == y && tm.tm_mon + 1 == (int)mo);
  assert(tm.tm_mday == (int)d && tm.tm_wday == c.weekday);
  assert(tm.tm_yday == c.yearday);
}

int main(void) {
  assert(civil_to_unix(1970, 1, 1, 0, 0, 0) == 0);
  assert(civil_to_unix(2000, 1, 1, 0, 0, 0) == 946684800);
  assert(civil_to_unix(2038, 1, 19, 3, 14, 8) == 2147483648LL);
  assert(civil_to_unix(2024, 2, 29, 23, 59, 59) == 1709251199);
  expect_round_trip(2026, 10, 29, 12, 34, 56);
  expect_round_trip(2000, 2, 29, 0, 0, 0);
  expect_round_trip(2100, 3, 1, 0, 0, 0);
  expect_round_trip(1999, 12, 31, 23, 59, 59);
  for (int64_t t = 0; t < 4102444800LL; t += 86400LL * 37 + 12345) {
    struct civil_time c;
    unix_to_civil(t, &c);
    assert(civil_to_unix(c.year, c.month, c.day, c.hour, c.minute,
                         c.second) == t);
  }
  assert(!civil_valid(2100, 2, 29, 0, 0, 0));
  assert(civil_valid(2000, 2, 29, 0, 0, 0));
  assert(!civil_valid(2026, 13, 1, 0, 0, 0));
  assert(!civil_valid(2026, 4, 31, 0, 0, 0));
  assert(!civil_valid(2026, 1, 1, 24, 0, 0));
  assert(!civil_valid(1969, 12, 31, 0, 0, 0));

  struct rtc_raw raw = {0x56, 0x34, 0x12, 0x29, 0x10, 0x26, 0x20,
                        RTC_STATUS_B_24H};
  struct civil_time c;
  assert(rtc_decode(&raw, &c));
  assert(c.year == 2026 && c.month == 10 && c.day == 29);
  assert(c.hour == 12 && c.minute == 34 && c.second == 56);

  raw = (struct rtc_raw){56, 34, 12, 29, 10, 26, 20,
                         RTC_STATUS_B_24H | RTC_STATUS_B_BINARY};
  assert(rtc_decode(&raw, &c) && c.year == 2026 && c.hour == 12);

  raw = (struct rtc_raw){0x00, 0x05, 0x81, 0x01, 0x01, 0x27, 0x00, 0};
  assert(rtc_decode(&raw, &c));
  assert(c.year == 2027 && c.hour == 13 && c.minute == 5);
  raw = (struct rtc_raw){0x00, 0x00, 0x12, 0x01, 0x01, 0x27, 0x20, 0};
  assert(rtc_decode(&raw, &c) && c.hour == 0);
  raw = (struct rtc_raw){0x00, 0x00, 0x92, 0x01, 0x01, 0x27, 0x20, 0};
  assert(rtc_decode(&raw, &c) && c.hour == 12);

  raw = (struct rtc_raw){0x5A, 0x34, 0x12, 0x29, 0x10, 0x26, 0x20,
                         RTC_STATUS_B_24H};
  assert(!rtc_decode(&raw, &c));
  raw = (struct rtc_raw){0x00, 0x00, 0x13, 0x29, 0x10, 0x26, 0x20, 0};
  assert(!rtc_decode(&raw, &c));
  raw = (struct rtc_raw){0x00, 0x00, 0x12, 0x31, 0x04, 0x26, 0x20,
                         RTC_STATUS_B_24H};
  assert(!rtc_decode(&raw, &c));

  raw = (struct rtc_raw){0x10, 0x20, 0x08, 0x05, 0x06, 0x99, 0x00,
                         RTC_STATUS_B_24H};
  assert(rtc_decode(&raw, &c) && c.year == 1999);

  for (uint8_t status = 0; status < 8; status++) {
    uint8_t status_b = (uint8_t)(status & 6);
    for (int64_t t = 946684800; t < 4102444800LL; t += 86400LL * 53 + 4567) {
      struct civil_time a, b;
      unix_to_civil(t, &a);
      struct rtc_raw encoded;
      rtc_encode(&a, status_b, &encoded);
      assert(rtc_decode(&encoded, &b));
      assert(civil_to_unix(b.year, b.month, b.day, b.hour, b.minute,
                           b.second) == t);
    }
  }
  return 0;
}
