#include "tz.h"
#include "civil.h"

static const struct tz_zone zones[] = {
    {"UTC", "Coordinated Universal Time", 0, TZ_RULE_NONE},
    {"Pacific/Honolulu", "Honolulu", -600, TZ_RULE_NONE},
    {"America/Anchorage", "Anchorage", -540, TZ_RULE_US},
    {"America/Los_Angeles", "Los Angeles", -480, TZ_RULE_US},
    {"America/Phoenix", "Phoenix", -420, TZ_RULE_NONE},
    {"America/Denver", "Denver", -420, TZ_RULE_US},
    {"America/Chicago", "Chicago", -360, TZ_RULE_US},
    {"America/Mexico_City", "Mexico City", -360, TZ_RULE_NONE},
    {"America/New_York", "New York", -300, TZ_RULE_US},
    {"America/Toronto", "Toronto", -300, TZ_RULE_US},
    {"America/Sao_Paulo", "Sao Paulo", -180, TZ_RULE_NONE},
    {"America/Argentina/Buenos_Aires", "Buenos Aires", -180, TZ_RULE_NONE},
    {"Atlantic/Reykjavik", "Reykjavik", 0, TZ_RULE_NONE},
    {"Europe/London", "London", 0, TZ_RULE_EU},
    {"Europe/Lisbon", "Lisbon", 0, TZ_RULE_EU},
    {"Africa/Lagos", "Lagos", 60, TZ_RULE_NONE},
    {"Europe/Berlin", "Berlin", 60, TZ_RULE_EU},
    {"Europe/Paris", "Paris", 60, TZ_RULE_EU},
    {"Europe/Madrid", "Madrid", 60, TZ_RULE_EU},
    {"Europe/Rome", "Rome", 60, TZ_RULE_EU},
    {"Europe/Warsaw", "Warsaw", 60, TZ_RULE_EU},
    {"Europe/Prague", "Prague", 60, TZ_RULE_EU},
    {"Europe/Stockholm", "Stockholm", 60, TZ_RULE_EU},
    {"Africa/Johannesburg", "Johannesburg", 120, TZ_RULE_NONE},
    {"Europe/Kyiv", "Kyiv", 120, TZ_RULE_EU},
    {"Europe/Bucharest", "Bucharest", 120, TZ_RULE_EU},
    {"Europe/Chisinau", "Chisinau", 120, TZ_RULE_EU},
    {"Europe/Athens", "Athens", 120, TZ_RULE_EU},
    {"Europe/Helsinki", "Helsinki", 120, TZ_RULE_EU},
    {"Europe/Riga", "Riga", 120, TZ_RULE_EU},
    {"Europe/Vilnius", "Vilnius", 120, TZ_RULE_EU},
    {"Europe/Tallinn", "Tallinn", 120, TZ_RULE_EU},
    {"Europe/Istanbul", "Istanbul", 180, TZ_RULE_NONE},
    {"Europe/Minsk", "Minsk", 180, TZ_RULE_NONE},
    {"Europe/Moscow", "Moscow", 180, TZ_RULE_NONE},
    {"Asia/Riyadh", "Riyadh", 180, TZ_RULE_NONE},
    {"Asia/Tbilisi", "Tbilisi", 240, TZ_RULE_NONE},
    {"Asia/Yerevan", "Yerevan", 240, TZ_RULE_NONE},
    {"Asia/Baku", "Baku", 240, TZ_RULE_NONE},
    {"Asia/Dubai", "Dubai", 240, TZ_RULE_NONE},
    {"Asia/Tashkent", "Tashkent", 300, TZ_RULE_NONE},
    {"Asia/Almaty", "Almaty", 300, TZ_RULE_NONE},
    {"Asia/Karachi", "Karachi", 300, TZ_RULE_NONE},
    {"Asia/Kolkata", "Kolkata", 330, TZ_RULE_NONE},
    {"Asia/Dhaka", "Dhaka", 360, TZ_RULE_NONE},
    {"Asia/Novosibirsk", "Novosibirsk", 420, TZ_RULE_NONE},
    {"Asia/Bangkok", "Bangkok", 420, TZ_RULE_NONE},
    {"Asia/Shanghai", "Shanghai", 480, TZ_RULE_NONE},
    {"Asia/Singapore", "Singapore", 480, TZ_RULE_NONE},
    {"Australia/Perth", "Perth", 480, TZ_RULE_NONE},
    {"Asia/Tokyo", "Tokyo", 540, TZ_RULE_NONE},
    {"Asia/Seoul", "Seoul", 540, TZ_RULE_NONE},
    {"Asia/Vladivostok", "Vladivostok", 600, TZ_RULE_NONE},
    {"Australia/Brisbane", "Brisbane", 600, TZ_RULE_NONE},
    {"Australia/Sydney", "Sydney", 600, TZ_RULE_AU},
    {"Pacific/Auckland", "Auckland", 720, TZ_RULE_NZ},
};

uint32_t tz_count(void) { return (uint32_t)(sizeof(zones) / sizeof(zones[0])); }

const struct tz_zone *tz_at(uint32_t index) {
  return index < tz_count() ? &zones[index] : 0;
}

static bool same(const char *a, const char *b) {
  while (*a && *a == *b) {
    a++;
    b++;
  }
  return *a == *b;
}

int32_t tz_find(const char *name) {
  if (!name)
    return -1;
  for (uint32_t i = 0; i < tz_count(); i++)
    if (same(zones[i].name, name))
      return (int32_t)i;
  if (same(name, "Europe/Kiev"))
    return tz_find("Europe/Kyiv");
  if (same(name, "Etc/UTC") || same(name, "GMT"))
    return 0;
  return -1;
}

static uint32_t weekday_of(int32_t year, uint32_t month, uint32_t day) {
  int64_t days = civil_to_unix(year, month, day, 0, 0, 0) / 86400;
  int64_t w = (days + 4) % 7;
  return (uint32_t)(w < 0 ? w + 7 : w);
}

static uint32_t days_in(int32_t year, uint32_t month) {
  static const uint8_t d[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
  return month == 2 && leap ? 29u : d[month - 1];
}

static uint32_t nth_sunday(int32_t year, uint32_t month, uint32_t n) {
  uint32_t first = weekday_of(year, month, 1);
  uint32_t day = 1 + (7 - first) % 7;
  return day + (n - 1) * 7;
}

static uint32_t last_sunday(int32_t year, uint32_t month) {
  uint32_t last = days_in(year, month);
  return last - weekday_of(year, month, last);
}

static int64_t at_local(int32_t year, uint32_t month, uint32_t day,
                        uint32_t hour, int32_t offset_minutes) {
  return civil_to_unix(year, month, day, hour, 0, 0) -
         (int64_t)offset_minutes * 60;
}

bool tz_is_dst(const struct tz_zone *zone, int64_t t) {
  if (!zone || zone->rule == TZ_RULE_NONE)
    return false;
  int32_t std = zone->std_minutes;
  struct civil_time local;
  unix_to_civil(t + (int64_t)std * 60, &local);
  int32_t y = local.year;
  int64_t start, end;
  switch (zone->rule) {
  case TZ_RULE_EU:
    start = at_local(y, 3, last_sunday(y, 3), 1, 0);
    end = at_local(y, 10, last_sunday(y, 10), 1, 0);
    return t >= start && t < end;
  case TZ_RULE_US:
    start = at_local(y, 3, nth_sunday(y, 3, 2), 2, std);
    end = at_local(y, 11, nth_sunday(y, 11, 1), 2, std + 60);
    return t >= start && t < end;
  case TZ_RULE_AU:
    end = at_local(y, 4, nth_sunday(y, 4, 1), 3, std + 60);
    start = at_local(y, 10, nth_sunday(y, 10, 1), 2, std);
    return t < end || t >= start;
  case TZ_RULE_NZ:
    end = at_local(y, 4, nth_sunday(y, 4, 1), 3, std + 60);
    start = at_local(y, 9, last_sunday(y, 9), 2, std);
    return t < end || t >= start;
  default:
    return false;
  }
}

int32_t tz_offset_minutes(const struct tz_zone *zone, int64_t t) {
  if (!zone)
    return 0;
  return zone->std_minutes + (tz_is_dst(zone, t) ? 60 : 0);
}

void tz_format_offset(int32_t minutes, char out[10]) {
  uint32_t m = (uint32_t)(minutes < 0 ? -minutes : minutes);
  out[0] = 'U';
  out[1] = 'T';
  out[2] = 'C';
  out[3] = minutes < 0 ? '-' : '+';
  out[4] = (char)('0' + (m / 60) / 10);
  out[5] = (char)('0' + (m / 60) % 10);
  out[6] = ':';
  out[7] = (char)('0' + (m % 60) / 10);
  out[8] = (char)('0' + (m % 60) % 10);
  out[9] = 0;
}
