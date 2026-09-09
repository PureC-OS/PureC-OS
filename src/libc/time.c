// PureC hosted libc: uptime-based time (no RTC in userspace).
// Epoch runs from boot; UTC, no DST. Enough for compiler timestamps
// (tcc -dt, __DATE__-style stamping) and timeouts.

#include "include/hosted/time.h"
#include "include/hosted/stdio.h"
#include "include/hosted/string.h"
#include "include/hosted/errno.h"
#include "include/purec.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static uint64_t time_uptime_ms(void) {
    struct cpu_monitor_info info;
    if (!pc_cpu_info(&info)) return 0;
    return info.uptime_ms;
}

time_t time(time_t *result) {
    time_t now = (time_t)(time_uptime_ms() / 1000ULL);
    if (result) *result = now;
    return now;
}

clock_t clock(void) {
    return (clock_t)time_uptime_ms(); // CLOCKS_PER_SEC == 1000
}

static bool time_is_leap(long year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

static int time_month_days(int month, long year) {
    static const uint8_t days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 1 && time_is_leap(year)) return 29;
    return days[month];
}

// Days since 1970-01-01 (Howard Hinnant's algorithm, proleptic Gregorian).
// month is 0-based (0=January) like struct tm; the formula below needs
// 1-based, hence the +1 first.
static long time_days_from_civil(long year, int month, int day) {
    int m1 = month + 1;
    year -= m1 <= 2;
    long era = (year >= 0 ? year : year - 399) / 400;
    unsigned yoe = (unsigned)(year - era * 400);
    unsigned mp = (unsigned)(m1 + (m1 > 2 ? -3 : 9));
    unsigned doy = (153 * mp + 2) / 5 + (unsigned)(day - 1);
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long)doe - 719468;
}

static void time_civil_from_days(long days, long *year, int *month, int *day) {
    long z = days + 719468;
    long era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long y = (long)yoe + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    unsigned d = doy - (153 * mp + 2) / 5 + 1;
    unsigned m = mp < 10 ? mp + 3 : mp - 9;
    *year = y + (m <= 2);
    *month = (int)m - 1;
    *day = (int)d;
}

time_t mktime(struct tm *parts) {
    if (!parts) { errno = EINVAL; return -1; }
    long days = time_days_from_civil((long)parts->tm_year + 1900, parts->tm_mon, parts->tm_mday);
    long long seconds = (long long)days * 86400LL
        + (long long)parts->tm_hour * 3600LL
        + (long long)parts->tm_min * 60LL
        + (long long)parts->tm_sec;
    // Normalize back so wday/yday are always filled.
    time_t result = (time_t)seconds;
    struct tm *back = gmtime(&result);
    if (back) *parts = *back;
    return result;
}

static struct tm time_static_tm;

static struct tm *time_fill(time_t value) {
    long days = value >= 0 ? (long)(value / 86400) : -((long)((-value + 86399) / 86400));
    long second_of_day = (long)(value - (time_t)days * 86400);
    long year;
    int month, day;
    time_civil_from_days(days, &year, &month, &day);
    time_static_tm.tm_year = (int)(year - 1900);
    time_static_tm.tm_mon = month;
    time_static_tm.tm_mday = day;
    time_static_tm.tm_hour = (int)(second_of_day / 3600);
    time_static_tm.tm_min = (int)((second_of_day % 3600) / 60);
    time_static_tm.tm_sec = (int)(second_of_day % 60);
    time_static_tm.tm_wday = (int)((days % 7 + 7 + 4) % 7); // 1970-01-01 was Thursday
    int yday = day - 1;
    for (int m = 0; m < month; m++) yday += time_month_days(m, year);
    time_static_tm.tm_yday = yday;
    time_static_tm.tm_isdst = 0;
    return &time_static_tm;
}

struct tm *gmtime(const time_t *value) {
    if (!value) { errno = EINVAL; return 0; }
    return time_fill(*value);
}

struct tm *localtime(const time_t *value) {
    return gmtime(value); // UTC everywhere, no zones
}

char *asctime(const struct tm *parts) {
    static char buffer[32];
    if (!parts) return 0;
    static const char *days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    int wday = parts->tm_wday >= 0 && parts->tm_wday < 7 ? parts->tm_wday : 0;
    int mon = parts->tm_mon >= 0 && parts->tm_mon < 12 ? parts->tm_mon : 0;
    snprintf(buffer, sizeof(buffer), "%.3s %.3s %2d %02d:%02d:%02d %d\n",
             days[wday], months[mon], parts->tm_mday,
             parts->tm_hour, parts->tm_min, parts->tm_sec,
             parts->tm_year + 1900);
    return buffer;
}

char *ctime(const time_t *value) {
    struct tm *parts = localtime(value);
    return parts ? asctime(parts) : 0;
}

size_t strftime(char *buffer, size_t capacity, const char *format,
                const struct tm *parts) {
    if (!buffer || !capacity || !format || !parts) return 0;
    static const char *day_names[] = {"Sunday", "Monday", "Tuesday", "Wednesday",
                                      "Thursday", "Friday", "Saturday"};
    static const char *day_abbr[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char *mon_names[] = {"January", "February", "March", "April", "May", "June",
                                      "July", "August", "September", "October", "November", "December"};
    static const char *mon_abbr[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                     "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    size_t length = 0;
    char number[16];
    for (; *format; format++) {
        if (*format != '%') {
            if (length + 1 >= capacity) return 0;
            buffer[length++] = *format;
            continue;
        }
        format++;
        const char *text = 0;
        int hour12 = parts->tm_hour % 12;
        if (hour12 == 0) hour12 = 12;
        switch (*format) {
            case 'Y': snprintf(number, sizeof(number), "%d", parts->tm_year + 1900); text = number; break;
            case 'y': snprintf(number, sizeof(number), "%02d", (parts->tm_year + 1900) % 100); text = number; break;
            case 'm': snprintf(number, sizeof(number), "%02d", parts->tm_mon + 1); text = number; break;
            case 'd': snprintf(number, sizeof(number), "%02d", parts->tm_mday); text = number; break;
            case 'e': snprintf(number, sizeof(number), "%2d", parts->tm_mday); text = number; break;
            case 'H': snprintf(number, sizeof(number), "%02d", parts->tm_hour); text = number; break;
            case 'I': snprintf(number, sizeof(number), "%02d", hour12); text = number; break;
            case 'M': snprintf(number, sizeof(number), "%02d", parts->tm_min); text = number; break;
            case 'S': snprintf(number, sizeof(number), "%02d", parts->tm_sec); text = number; break;
            case 'j': snprintf(number, sizeof(number), "%03d", parts->tm_yday + 1); text = number; break;
            case 'w': snprintf(number, sizeof(number), "%d", parts->tm_wday); text = number; break;
            case 'u': snprintf(number, sizeof(number), "%d", parts->tm_wday ? parts->tm_wday : 7); text = number; break;
            case 'a': text = day_abbr[parts->tm_wday >= 0 && parts->tm_wday < 7 ? parts->tm_wday : 0]; break;
            case 'A': text = day_names[parts->tm_wday >= 0 && parts->tm_wday < 7 ? parts->tm_wday : 0]; break;
            case 'b':
            case 'h': text = mon_abbr[parts->tm_mon >= 0 && parts->tm_mon < 12 ? parts->tm_mon : 0]; break;
            case 'B': text = mon_names[parts->tm_mon >= 0 && parts->tm_mon < 12 ? parts->tm_mon : 0]; break;
            case 'p': text = parts->tm_hour < 12 ? "AM" : "PM"; break;
            case '%': text = "%"; break;
            default: continue; // skip unknown conversions
        }
        if (!text) continue;
        for (; *text; text++) {
            if (length + 1 >= capacity) return 0;
            buffer[length++] = *text;
        }
    }
    if (!capacity) return 0;
    buffer[length] = '\0';
    return length;
}
