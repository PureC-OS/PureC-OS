#include "datetime_service.h"
#include "../syscall.h"
#include "../../kernel/syscall/syscall.h"
#include "../../kernel/core/system_info.h"
#include "../../lib/string.h"

#define DATETIME_PATH "/purec/datetime.cfg"

static struct desktop_datetime current_datetime = {
    2026,
    8,
    28,
    12,
    0,
    0
};
static uint64_t last_uptime_second;

static bool is_leap_year(uint16_t year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

uint8_t datetime_service_days_in_month(uint16_t year, uint8_t month) {
    static const uint8_t days[12] = {
        31, 28, 31, 30, 31, 30,
        31, 31, 30, 31, 30, 31
    };

    if (month < 1 || month > 12) {
        return 0;
    }
    if (month == 2 && is_leap_year(year)) {
        return 29;
    }
    return days[month - 1];
}

static void advance_one_second(void) {
    current_datetime.second++;
    if (current_datetime.second < 60) {
        return;
    }
    current_datetime.second = 0;

    current_datetime.minute++;
    if (current_datetime.minute < 60) {
        return;
    }
    current_datetime.minute = 0;

    current_datetime.hour++;
    if (current_datetime.hour < 24) {
        return;
    }
    current_datetime.hour = 0;

    current_datetime.day++;
    if (current_datetime.day <= datetime_service_days_in_month(
            current_datetime.year,
            current_datetime.month)) {
        return;
    }
    current_datetime.day = 1;

    current_datetime.month++;
    if (current_datetime.month <= 12) {
        return;
    }
    current_datetime.month = 1;
    current_datetime.year++;
}

static char *append_unsigned(
    char *output,
    uint32_t number,
    uint8_t minimum_digits
) {
    char reversed[12];
    uint8_t length = 0;

    do {
        reversed[length++] = (char)('0' + number % 10);
        number /= 10;
    } while (number != 0);

    while (length < minimum_digits) {
        reversed[length++] = '0';
    }
    while (length > 0) {
        *output++ = reversed[--length];
    }

    *output = '\0';
    return output;
}

void datetime_service_format(char output[20]) {
    char *cursor = output;

    cursor = append_unsigned(cursor, current_datetime.year, 4);
    *cursor++ = '-';
    cursor = append_unsigned(cursor, current_datetime.month, 2);
    *cursor++ = '-';
    cursor = append_unsigned(cursor, current_datetime.day, 2);
    *cursor++ = ' ';
    cursor = append_unsigned(cursor, current_datetime.hour, 2);
    *cursor++ = ':';
    cursor = append_unsigned(cursor, current_datetime.minute, 2);
    *cursor++ = ':';
    append_unsigned(cursor, current_datetime.second, 2);
}

static bool parse_number(
    const char *text,
    uint32_t start,
    uint32_t count,
    uint32_t *result
) {
    uint32_t value = 0;

    for (uint32_t index = 0; index < count; index++) {
        char character = text[start + index];
        if (character < '0' || character > '9') {
            return false;
        }
        value = value * 10 + (uint32_t)(character - '0');
    }

    *result = value;
    return true;
}

bool datetime_service_parse(
    const char *text,
    struct desktop_datetime *result,
    bool date_only
) {
    uint32_t year;
    uint32_t month;
    uint32_t day;
    uint32_t hour = 0;
    uint32_t minute = 0;
    uint32_t second = 0;
    uint32_t expected_length = date_only ? 10 : 19;

    if (strlen(text) != expected_length
        || text[4] != '-'
        || text[7] != '-') {
        return false;
    }

    if (!parse_number(text, 0, 4, &year)
        || !parse_number(text, 5, 2, &month)
        || !parse_number(text, 8, 2, &day)) {
        return false;
    }

    if (!date_only) {
        if (text[10] != ' ' || text[13] != ':' || text[16] != ':') {
            return false;
        }
        if (!parse_number(text, 11, 2, &hour)
            || !parse_number(text, 14, 2, &minute)
            || !parse_number(text, 17, 2, &second)) {
            return false;
        }
    }

    if (year < 1980 || year > 9999
        || month < 1 || month > 12
        || day < 1
        || day > datetime_service_days_in_month(
            (uint16_t)year,
            (uint8_t)month)
        || hour > 23 || minute > 59 || second > 59) {
        return false;
    }

    *result = (struct desktop_datetime) {
        (uint16_t)year,
        (uint8_t)month,
        (uint8_t)day,
        (uint8_t)hour,
        (uint8_t)minute,
        (uint8_t)second
    };
    return true;
}

void datetime_service_save(void) {
    char text[20];

    datetime_service_format(text);
    userspace_syscall(SYS_DIR_CREATE, (uint64_t)"/purec", 0, 0);
    userspace_syscall(
        SYS_FILE_WRITE,
        (uint64_t)DATETIME_PATH,
        (uint64_t)text,
        19
    );
}

void datetime_service_init(void) {
    int64_t descriptor = userspace_syscall(
        SYS_FILE_OPEN,
        (uint64_t)DATETIME_PATH,
        0,
        0
    );

    if (descriptor >= 0) {
        char text[20] = {0};
        struct desktop_datetime loaded;
        int64_t count = userspace_syscall(
            SYS_FILE_READ,
            (uint64_t)descriptor,
            (uint64_t)text,
            19
        );

        userspace_syscall(SYS_FILE_CLOSE, (uint64_t)descriptor, 0, 0);
        if (count == 19
            && datetime_service_parse(text, &loaded, false)) {
            current_datetime = loaded;
        }
    }

    last_uptime_second = system_info_uptime_ms() / 1000;
}

bool datetime_service_update(void) {
    uint64_t uptime_second = system_info_uptime_ms() / 1000;
    bool changed = false;

    while (last_uptime_second < uptime_second) {
        advance_one_second();
        last_uptime_second++;
        changed = true;
    }

    return changed;
}

const struct desktop_datetime *datetime_service_get(void) {
    return &current_datetime;
}

bool datetime_service_set(const struct desktop_datetime *datetime) {
    if (datetime == 0) {
        return false;
    }

    current_datetime = *datetime;
    last_uptime_second = system_info_uptime_ms() / 1000;
    datetime_service_save();
    return true;
}
