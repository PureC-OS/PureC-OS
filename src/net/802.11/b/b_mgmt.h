#pragma once
#include <stdbool.h>
#include <stdint.h>
struct dot11_bss;
#define DOT11B_FAIL_OK 0u
#define DOT11B_FAIL_NOT_ESS 1u
#define DOT11B_FAIL_SECURITY 2u
#define DOT11B_FAIL_NO_B_RATES 3u
#define DOT11B_FAIL_MISSING_BASIC 4u
#define DOT11B_FAIL_BAD_CHANNEL 5u
uint16_t dot11b_cap_sta(bool short_preamble_ok);
uint16_t dot11b_build_probe_req(const uint8_t sa[6], const char *ssid, uint8_t ssid_len, const uint8_t *rates, uint8_t rate_count, uint8_t *out, uint16_t out_cap);
bool dot11b_bss_compatible(const struct dot11_bss *bss, uint8_t expected_channel, uint8_t *fail_out);
