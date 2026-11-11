#include "kernel/time/civil.h"
#include "kernel/time/tz.h"
#include <stdio.h>

static int failures;

static void check(const char *zone, int y, int mo, int d, int h, int mi,
                  int expect) {
  int32_t i = tz_find(zone);
  if (i < 0) {
    printf("FAIL no zone %s\n", zone);
    failures++;
    return;
  }
  int64_t t = civil_to_unix(y, (uint32_t)mo, (uint32_t)d, (uint32_t)h,
                            (uint32_t)mi, 0);
  int32_t got = tz_offset_minutes(tz_at((uint32_t)i), t);
  if (got != expect) {
    printf("FAIL %s %04d-%02d-%02d %02d:%02d UTC: %d != %d\n", zone, y, mo, d,
           h, mi, got, expect);
    failures++;
  }
}

int main(void) {
  check("UTC", 2026, 7, 1, 0, 0, 0);
  check("Europe/Kyiv", 2026, 1, 15, 12, 0, 120);
  check("Europe/Kyiv", 2026, 7, 15, 12, 0, 180);
  check("Europe/Kyiv", 2026, 3, 29, 0, 59, 120);
  check("Europe/Kyiv", 2026, 3, 29, 1, 0, 180);
  check("Europe/Kyiv", 2026, 10, 25, 0, 59, 180);
  check("Europe/Kyiv", 2026, 10, 25, 1, 0, 120);
  check("Europe/London", 2026, 6, 1, 0, 0, 60);
  check("Europe/Berlin", 2026, 12, 1, 0, 0, 60);
  check("America/New_York", 2026, 3, 8, 6, 59, -300);
  check("America/New_York", 2026, 3, 8, 7, 0, -240);
  check("America/New_York", 2026, 11, 1, 5, 59, -240);
  check("America/New_York", 2026, 11, 1, 6, 0, -300);
  check("America/Los_Angeles", 2026, 7, 4, 20, 0, -420);
  check("America/Phoenix", 2026, 7, 4, 20, 0, -420);
  check("Australia/Sydney", 2026, 1, 10, 0, 0, 660);
  check("Australia/Sydney", 2026, 6, 10, 0, 0, 600);
  check("Australia/Sydney", 2026, 4, 4, 15, 59, 660);
  check("Australia/Sydney", 2026, 4, 4, 16, 0, 600);
  check("Australia/Sydney", 2026, 10, 3, 15, 59, 600);
  check("Australia/Sydney", 2026, 10, 3, 16, 0, 660);
  check("Pacific/Auckland", 2026, 1, 10, 0, 0, 780);
  check("Pacific/Auckland", 2026, 7, 10, 0, 0, 720);
  check("Asia/Kolkata", 2026, 7, 10, 0, 0, 330);
  check("Europe/Kiev", 2026, 7, 10, 0, 0, 180);
  char buf[10];
  tz_format_offset(-570, buf);
  if (buf[3] != '-' || buf[4] != '0' || buf[5] != '9' || buf[7] != '3') {
    printf("FAIL format %s\n", buf);
    failures++;
  }
  printf("tz_test: %s (%d failures)\n", failures ? "FAILED" : "ok", failures);
  return failures ? 1 : 0;
}
