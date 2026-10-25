#include "b_rates.h"
#include "../include/802.h"

static const uint8_t b_basic[DOT11B_RATE_COUNT] = {
    DOT11B_RATE_1M, DOT11B_RATE_2M, DOT11B_RATE_5_5M, DOT11B_RATE_11M,
};

static uint8_t strip_basic(uint8_t enc) {
    return (uint8_t)(enc & 0x7Fu);
}

bool dot11b_rate_is_b(uint8_t enc) {
    switch (strip_basic(enc)) {
    case 0x02u:
    case 0x04u:
    case 0x0Bu:
    case 0x16u:
        return true;
    default:
        return false;
    }
}

bool dot11b_rate_is_basic(uint8_t enc) {
    return dot11b_rate_is_b(enc) && (enc & 0x80u) != 0;
}

uint8_t dot11b_basic_set(uint8_t *out, uint8_t cap) {
    if (!out || cap < DOT11B_RATE_COUNT)
        return 0;
    for (uint8_t i = 0; i < DOT11B_RATE_COUNT; i++)
        out[i] = b_basic[i];
    return DOT11B_RATE_COUNT;
}

uint8_t dot11b_rate_units(uint8_t enc) {
    uint8_t v = strip_basic(enc);
    switch (v) {
    case 0x02u:
    case 0x04u:
    case 0x0Bu:
    case 0x16u:
        return v;
    default:
        return 0;
    }
}

uint16_t dot11b_rate_mbps_x10(uint8_t enc) {
    switch (strip_basic(enc)) {
    case 0x02u:
        return 10u;
    case 0x04u:
        return 20u;
    case 0x0Bu:
        return 55u;
    case 0x16u:
        return 110u;
    default:
        return 0;
    }
}

bool dot11b_rates_are_b_only(const uint8_t *rates, uint8_t count) {
    if (!rates || !count)
        return false;
    for (uint8_t i = 0; i < count; i++) {
        if (!dot11b_rate_is_b(rates[i]))
            return false;
    }
    return true;
}

uint8_t dot11b_filter_compatible(const uint8_t *bss_rates, uint8_t bss_count,
                                 uint8_t *out, uint8_t out_cap) {
    if (!bss_rates || !bss_count || !out || !out_cap)
        return 0;
    uint8_t n = 0;
    for (uint8_t i = 0; i < bss_count; i++) {
        if (!dot11b_rate_is_b(bss_rates[i]))
            continue;
        uint8_t v = strip_basic(bss_rates[i]);
        bool dup = false;
        for (uint8_t j = 0; j < n; j++) {
            if (strip_basic(out[j]) == v) {
                out[j] |= (uint8_t)(bss_rates[i] & 0x80u);
                dup = true;
                break;
            }
        }
        if (dup)
            continue;
        if (n >= out_cap)
            return n;
        out[n++] = bss_rates[i];
    }
    return n;
}

bool dot11b_bss_has_mandatory(const struct dot11_bss *bss) {
    if (!bss || bss->rate_count < 2)
        return false;
    bool has_1m = false;
    bool has_2m = false;
    for (uint8_t i = 0; i < bss->rate_count; i++) {
        uint8_t v = strip_basic(bss->rates[i]);
        if (v == 0x02u)
            has_1m = true;
        if (v == 0x04u)
            has_2m = true;
    }
    return has_1m && has_2m;
}

uint8_t dot11b_choose_tx_rate(int8_t rssi_dbm) {
    if (rssi_dbm >= -60)
        return DOT11B_RATE_11M;
    if (rssi_dbm >= -70)
        return DOT11B_RATE_5_5M;
    if (rssi_dbm >= -80)
        return DOT11B_RATE_2M;
    return DOT11B_RATE_1M;
}
