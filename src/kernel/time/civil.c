#include "civil.h"

static bool leap_year(int32_t year) {
  return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

static uint32_t month_length(int32_t year, uint32_t month) {
  static const uint8_t days[12] = {31, 28, 31, 30, 31, 30,
                                   31, 31, 30, 31, 30, 31};
  if (month == 2 && leap_year(year))
    return 29;
  return days[month - 1];
}

bool civil_valid(int32_t year, uint32_t month, uint32_t day, uint32_t hour,
                 uint32_t minute, uint32_t second) {
  if (year < 1970 || year > 2105 || month < 1 || month > 12 || day < 1)
    return false;
  if (day > month_length(year, month))
    return false;
  return hour < 24 && minute < 60 && second < 60;
}

static int64_t days_from_civil(int32_t year, uint32_t month, uint32_t day) {
  int64_t y = year - (month <= 2 ? 1 : 0);
  int64_t era = (y >= 0 ? y : y - 399) / 400;
  uint32_t yoe = (uint32_t)(y - era * 400);
  uint32_t mp = month > 2 ? month - 3 : month + 9;
  uint32_t doy = (153 * mp + 2) / 5 + day - 1;
  uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int64_t)doe - 719468;
}

int64_t civil_to_unix(int32_t year, uint32_t month, uint32_t day,
                      uint32_t hour, uint32_t minute, uint32_t second) {
  return days_from_civil(year, month, day) * 86400 + (int64_t)hour * 3600 +
         (int64_t)minute * 60 + (int64_t)second;
}

void unix_to_civil(int64_t seconds, struct civil_time *out) {
  int64_t days = seconds >= 0 ? seconds / 86400 : -((-seconds + 86399) / 86400);
  int64_t rest = seconds - days * 86400;
  int64_t z = days + 719468;
  int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  uint32_t doe = (uint32_t)(z - era * 146097);
  uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int64_t y = (int64_t)yoe + era * 400;
  uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  uint32_t mp = (5 * doy + 2) / 153;
  uint32_t d = doy - (153 * mp + 2) / 5 + 1;
  uint32_t m = mp < 10 ? mp + 3 : mp - 9;
  out->year = (int32_t)(y + (m <= 2 ? 1 : 0));
  out->month = (uint8_t)m;
  out->day = (uint8_t)d;
  out->hour = (uint8_t)(rest / 3600);
  out->minute = (uint8_t)((rest % 3600) / 60);
  out->second = (uint8_t)(rest % 60);
  out->weekday = (uint8_t)(((days % 7) + 7 + 4) % 7);
  uint16_t yday = (uint16_t)(d - 1);
  for (uint32_t i = 1; i < m; i++)
    yday = (uint16_t)(yday + month_length(out->year, i));
  out->yearday = yday;
}
