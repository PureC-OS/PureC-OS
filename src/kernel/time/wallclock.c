#include "wallclock.h"
#include "../core/system_info.h"
#include "../diagnostics/klog.h"
#include "../sync/spinlock.h"
#include "civil.h"
#include "rtc.h"
#include "tz.h"
#include "../../fs/vfs.h"

static spinlock_t lock = SPINLOCK_INIT;
static int64_t base_unix_ms;
static uint64_t base_uptime_ms;
static int32_t tz_minutes;
static uint32_t source;
static int32_t zone_index = -1;

void wallclock_init(void) {
  struct civil_time now;
  if (!rtc_read(&now)) {
    klog(KLOG_WARN, "time: RTC unreadable, wall clock unset");
    return;
  }
  uint64_t uptime = system_info_uptime_ms();
  int64_t seconds = civil_to_unix(now.year, now.month, now.day, now.hour,
                                  now.minute, now.second);
  uint64_t flags = spin_lock_irqsave(&lock);
  base_unix_ms = seconds * 1000;
  base_uptime_ms = uptime;
  source = WALLCLOCK_SOURCE_RTC;
  spin_unlock_irqrestore(&lock, flags);
  klogf(KLOG_OK, "time: RTC %04d-%02u-%02u %02u:%02u:%02u UTC", now.year,
        now.month, now.day, now.hour, now.minute, now.second);
}

bool wallclock_valid(void) { return source != WALLCLOCK_SOURCE_NONE; }

uint32_t wallclock_source(void) { return source; }

int64_t wallclock_unix_ms(void) {
  uint64_t uptime = system_info_uptime_ms();
  uint64_t flags = spin_lock_irqsave(&lock);
  int64_t now = base_unix_ms + (int64_t)(uptime - base_uptime_ms);
  bool known = source != WALLCLOCK_SOURCE_NONE;
  spin_unlock_irqrestore(&lock, flags);
  return known ? now : (int64_t)uptime;
}

uint32_t wallclock_unix_seconds(void) {
  int64_t ms = wallclock_unix_ms();
  if (ms < 0)
    return 0;
  int64_t seconds = ms / 1000;
  return seconds > 0xFFFFFFFFLL ? 0xFFFFFFFFu : (uint32_t)seconds;
}

int32_t wallclock_tz_minutes(void) {
  int32_t zone = __atomic_load_n(&zone_index, __ATOMIC_ACQUIRE);
  if (zone < 0)
    return tz_minutes;
  return tz_offset_minutes(tz_at((uint32_t)zone), wallclock_unix_ms() / 1000);
}

const char *wallclock_zone_name(void) {
  int32_t zone = __atomic_load_n(&zone_index, __ATOMIC_ACQUIRE);
  return zone < 0 ? "" : tz_at((uint32_t)zone)->name;
}

bool wallclock_set_zone(const char *name) {
  int32_t zone = tz_find(name);
  if (zone < 0)
    return false;
  __atomic_store_n(&zone_index, zone, __ATOMIC_RELEASE);
  char off[10];
  tz_format_offset(wallclock_tz_minutes(), off);
  klogf(KLOG_OK, "time: zone %s (%s)", tz_at((uint32_t)zone)->name, off);
  return true;
}

int32_t wallclock_set_utc(int64_t unix_seconds) {
  struct civil_time parts;
  unix_to_civil(unix_seconds, &parts);
  if (!civil_valid(parts.year, parts.month, parts.day, parts.hour,
                   parts.minute, parts.second))
    return -1;
  uint64_t uptime = system_info_uptime_ms();
  uint64_t flags = spin_lock_irqsave(&lock);
  base_unix_ms = unix_seconds * 1000;
  base_uptime_ms = uptime;
  source = WALLCLOCK_SOURCE_SET;
  spin_unlock_irqrestore(&lock, flags);
  if (!rtc_write(&parts))
    klog(KLOG_WARN, "time: RTC write failed, clock set for this boot only");
  return 0;
}

void wallclock_load_zone(void) {
  int32_t fd = vfs_open(WALLCLOCK_ZONE_PATH);
  if (fd < 0) {
    klog(KLOG_INFO, "time: no " WALLCLOCK_ZONE_PATH ", local time = UTC");
    return;
  }
  char name[TZ_NAME_MAX + 8];
  int32_t n = vfs_read(fd, name, sizeof(name) - 1);
  vfs_close(fd);
  if (n <= 0)
    return;
  name[n] = 0;
  for (int32_t i = 0; i < n; i++)
    if (name[i] == '\n' || name[i] == '\r' || name[i] == ' ') {
      name[i] = 0;
      break;
    }
  if (!wallclock_set_zone(name))
    klogf(KLOG_WARN, "time: unknown zone '%s' in " WALLCLOCK_ZONE_PATH, name);
}

int32_t wallclock_set(int64_t unix_seconds, int32_t zone_minutes) {
  if (zone_minutes <= -WALLCLOCK_TZ_LIMIT || zone_minutes >= WALLCLOCK_TZ_LIMIT)
    return -1;
  struct civil_time parts;
  unix_to_civil(unix_seconds, &parts);
  if (!civil_valid(parts.year, parts.month, parts.day, parts.hour,
                   parts.minute, parts.second))
    return -1;
  int32_t effective = wallclock_tz_minutes();
  uint64_t uptime = system_info_uptime_ms();
  uint64_t flags = spin_lock_irqsave(&lock);
  base_unix_ms = unix_seconds * 1000;
  base_uptime_ms = uptime;
  if (zone_index >= 0 && zone_minutes != effective)
    zone_index = -1;
  tz_minutes = zone_minutes;
  source = WALLCLOCK_SOURCE_SET;
  spin_unlock_irqrestore(&lock, flags);
  if (!rtc_write(&parts))
    klog(KLOG_WARN, "time: RTC write failed, clock set for this boot only");
  return 0;
}
