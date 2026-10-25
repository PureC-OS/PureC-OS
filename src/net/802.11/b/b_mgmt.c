#include "b_mgmt.h"
#include "b_chan.h"
#include "b_rates.h"
#include "../include/802.h"
static void put_le16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}
uint16_t dot11b_cap_sta(bool short_preamble_ok) {
    uint16_t cap = DOT11_CAP_ESS;
    if (short_preamble_ok)
        cap |= (uint16_t)DOT11_CAP_SHORT_PREAMBLE;
    return cap;
}
uint16_t dot11b_build_probe_req(const uint8_t sa[6], const char *ssid, uint8_t ssid_len, const uint8_t *rates, uint8_t rate_count, uint8_t *out, uint16_t out_cap) {
    static const uint8_t bcast[DOT11_ADDR_LEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    static const uint8_t fallback[DOT11B_RATE_COUNT] = {
        DOT11B_RATE_1M, DOT11B_RATE_2M, DOT11B_RATE_5_5M, DOT11B_RATE_11M,
    };
    if (!sa || !out)
        return 0;
    if (ssid_len > DOT11_SSID_MAX)
        return 0;
    if (ssid_len && !ssid)
        return 0;
    const uint8_t *r = rates;
    uint8_t n = rate_count;
    if (!r || !n) {
        r = fallback;
        n = DOT11B_RATE_COUNT;
    }
    if (n > 8u)
        return 0;
    for (uint8_t i = 0; i < n; i++) {
        if (!dot11b_rate_is_b(r[i]))
            return 0;
    }
    uint16_t need = (uint16_t)(24u + 2u + ssid_len + 2u + n);
    if (out_cap < need)
        return 0;
    put_le16(out + 0, DOT11_FC_MAKE(DOT11_FTYPE_MGMT, DOT11_STYPE_PROBE_REQ));
    put_le16(out + 2, 0);
    for (int i = 0; i < DOT11_ADDR_LEN; i++) {
        out[4 + i] = bcast[i];
        out[10 + i] = sa[i];
        out[16 + i] = bcast[i];
    }
    put_le16(out + 22, 0);
    uint16_t off = 24u;
    out[off++] = DOT11_IE_SSID;
    out[off++] = ssid_len;
    for (uint8_t i = 0; i < ssid_len; i++)
        out[off++] = (uint8_t)ssid[i];
    out[off++] = DOT11_IE_RATES;
    out[off++] = n;
    for (uint8_t i = 0; i < n; i++)
        out[off++] = r[i];
    return off;
}
bool dot11b_bss_compatible(const struct dot11_bss *bss, uint8_t expected_channel, uint8_t *fail_out) {
    uint8_t fail = DOT11B_FAIL_OK;
    bool ok = false;
    if (!bss) {
        fail = DOT11B_FAIL_NO_B_RATES;
    } else if (!(bss->capability & DOT11_CAP_ESS) || (bss->capability & DOT11_CAP_IBSS)) {
        fail = DOT11B_FAIL_NOT_ESS;
    } else if ((bss->capability & DOT11_CAP_PRIVACY) || bss->has_rsn) {
        fail = DOT11B_FAIL_SECURITY;
    } else if (expected_channel && bss->channel && bss->channel != expected_channel) {
        fail = DOT11B_FAIL_BAD_CHANNEL;
    } else if (bss->channel && !dot11b_channel_valid(bss->channel)) {
        fail = DOT11B_FAIL_BAD_CHANNEL;
    } else {
        uint8_t tmp[DOT11_RATES_MAX];
        uint8_t n = dot11b_filter_compatible(bss->rates, bss->rate_count, tmp, sizeof(tmp));
        if (!n) {
            fail = DOT11B_FAIL_NO_B_RATES;
        } else if (!dot11b_bss_has_mandatory(bss)) {
            fail = DOT11B_FAIL_MISSING_BASIC;
        } else {
            ok = true;
        }
    }
    if (fail_out)
        *fail_out = fail;
    return ok;
}
