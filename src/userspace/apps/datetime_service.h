#pragma once

#include <stdbool.h>
#include <stdint.h>

struct desktop_datetime {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
};

void datetime_service_init(void);
bool datetime_service_update(void);
const struct desktop_datetime *datetime_service_get(void);
bool datetime_service_set(const struct desktop_datetime *datetime);
bool datetime_service_parse(
    const char *text,
    struct desktop_datetime *result,
    bool date_only
);
void datetime_service_format(char output[20]);
uint8_t datetime_service_days_in_month(uint16_t year, uint8_t month);
void datetime_service_save(void);
