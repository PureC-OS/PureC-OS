#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
struct dot11_bss;
#define DOT11B_RATE_1M 0x82u
#define DOT11B_RATE_2M 0x84u
#define DOT11B_RATE_5_5M 0x8Bu
#define DOT11B_RATE_11M 0x96u
#define DOT11B_RATE_COUNT 4u
bool dot11b_rate_is_b(uint8_t enc);
bool dot11b_rate_is_basic(uint8_t enc);
uint8_t dot11b_basic_set(uint8_t *out, uint8_t cap);
uint8_t dot11b_rate_units(uint8_t enc);
uint16_t dot11b_rate_mbps_x10(uint8_t enc);
bool dot11b_rates_are_b_only(const uint8_t *rates, uint8_t count);
uint8_t dot11b_filter_compatible(const uint8_t *bss_rates, uint8_t bss_count, uint8_t *out, uint8_t out_cap);
bool dot11b_bss_has_mandatory(const struct dot11_bss *bss);
uint8_t dot11b_choose_tx_rate(int8_t rssi_dbm);
