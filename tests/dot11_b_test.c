#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "net/802.11/include/dot11_b.h"
static int failures;
#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", __func__, __LINE__, #expr); \
        failures++; \
        return; \
    } \
} while (0)
static void test_rates(void) {
    CHECK(dot11b_rate_is_b(0x82));
    CHECK(dot11b_rate_is_b(0x96));
    CHECK(dot11b_rate_is_b(0x0B));
    CHECK(!dot11b_rate_is_b(0x0C));
    CHECK(!dot11b_rate_is_b(0x6C));
    CHECK(!dot11b_rate_is_b(0x00));
    CHECK(dot11b_rate_is_basic(0x82));
    CHECK(!dot11b_rate_is_basic(0x02));
    uint8_t set[4];
    CHECK(dot11b_basic_set(set, sizeof(set)) == 4);
    CHECK(set[0] == 0x82 && set[3] == 0x96);
    CHECK(dot11b_basic_set(NULL, 4) == 0);
    CHECK(dot11b_basic_set(set, 2) == 0);
    CHECK(dot11b_rate_mbps_x10(0x82) == 10);
    CHECK(dot11b_rate_mbps_x10(0x96) == 110);
    CHECK(dot11b_rate_mbps_x10(0x8B) == 55);
    CHECK(dot11b_rate_mbps_x10(0x0C) == 0);
    const uint8_t b_only[] = {0x82, 0x84, 0x8B, 0x96};
    const uint8_t mixed[] = {0x82, 0x0C};
    CHECK(dot11b_rates_are_b_only(b_only, sizeof(b_only)));
    CHECK(!dot11b_rates_are_b_only(mixed, sizeof(mixed)));
    CHECK(!dot11b_rates_are_b_only(NULL, 0));
    uint8_t out[8];
    uint8_t n = dot11b_filter_compatible(mixed, sizeof(mixed), out, sizeof(out));
    CHECK(n == 1 && out[0] == 0x82);
    const uint8_t ofdm[] = {0x0C, 0x12};
    CHECK(dot11b_filter_compatible(ofdm, sizeof(ofdm), out, sizeof(out)) == 0);
    CHECK(dot11b_choose_tx_rate(-50) == 0x96);
    CHECK(dot11b_choose_tx_rate(-65) == 0x8B);
    CHECK(dot11b_choose_tx_rate(-75) == 0x84);
    CHECK(dot11b_choose_tx_rate(-90) == 0x82);
}
static void test_chan(void) {
    CHECK(!dot11b_channel_valid(0));
    CHECK(dot11b_channel_valid(1));
    CHECK(dot11b_channel_valid(14));
    CHECK(!dot11b_channel_valid(15));
    CHECK(dot11b_channel_to_freq_mhz(1) == 2412);
    CHECK(dot11b_channel_to_freq_mhz(6) == 2437);
    CHECK(dot11b_channel_to_freq_mhz(13) == 2472);
    CHECK(dot11b_channel_to_freq_mhz(14) == 2484);
    CHECK(dot11b_channel_to_freq_mhz(0) == 0);
    CHECK(dot11b_freq_to_channel(2412) == 1);
    CHECK(dot11b_freq_to_channel(2437) == 6);
    CHECK(dot11b_freq_to_channel(2484) == 14);
    CHECK(dot11b_freq_to_channel(2400) == 0);
    CHECK(dot11b_freq_to_channel(2413) == 0);
    CHECK(dot11b_channel_is_dsss_only(14));
    CHECK(!dot11b_channel_is_dsss_only(1));
}
static void test_plcp(void) {
    CHECK(dot11b_signal_for_rate(0x82) == 0x0A);
    CHECK(dot11b_signal_for_rate(0x96) == 0x6E);
    CHECK(dot11b_signal_for_rate(0x0C) == 0);
    CHECK(dot11b_rate_for_signal(0x0A) == 0x82);
    CHECK(dot11b_rate_for_signal(0xFF) == 0);
    CHECK(dot11b_signal_valid(0x37));
    CHECK(!dot11b_signal_valid(0x01));
    CHECK(dot11b_tx_time_us(100, 0x82, false) == 992);
    CHECK(dot11b_tx_time_us(100, 0x96, false) == 192 + (8000 + 55) / 110);
    CHECK(dot11b_tx_time_us(100, 0x82, true) == 96 + 800);
    CHECK(dot11b_tx_time_us(100, 0x0C, false) == 0);
    CHECK(dot11b_plcp_len_valid(800, 0x0A));
    CHECK(!dot11b_plcp_len_valid(0, 0x0A));
    CHECK(!dot11b_plcp_len_valid(800, 0x01));
}
static void test_mgmt(void) {
    CHECK(dot11b_cap_sta(false) == DOT11_CAP_ESS);
    CHECK(dot11b_cap_sta(true) == (DOT11_CAP_ESS | DOT11_CAP_SHORT_PREAMBLE));
    uint8_t frame[128];
    const uint8_t sa[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    uint16_t len = dot11b_build_probe_req(sa, "TestNet", 7, NULL, 0, frame, sizeof(frame));
    CHECK(len == 24 + 2 + 7 + 2 + 4);
    CHECK(DOT11_FC_STYPE(frame[0] | ((uint16_t)frame[1] << 8)) == DOT11_STYPE_PROBE_REQ);
    CHECK(frame[24] == DOT11_IE_SSID && frame[25] == 7);
    CHECK(frame[24 + 2 + 7] == DOT11_IE_RATES && frame[24 + 2 + 7 + 1] == 4);
    const uint8_t ofdm[] = {0x0C};
    CHECK(dot11b_build_probe_req(sa, NULL, 0, ofdm, 1, frame, sizeof(frame)) == 0);
    CHECK(dot11b_build_probe_req(NULL, NULL, 0, NULL, 0, frame, sizeof(frame)) == 0);
    CHECK(dot11b_build_probe_req(sa, NULL, 0, NULL, 0, frame, sizeof(frame)) == 24 + 2 + 2 + 4);
    struct dot11_bss bss;
    memset(&bss, 0, sizeof(bss));
    bss.capability = DOT11_CAP_ESS;
    bss.channel = 6;
    bss.rates[0] = 0x82;
    bss.rates[1] = 0x84;
    bss.rates[2] = 0x8B;
    bss.rates[3] = 0x96;
    bss.rate_count = 4;
    uint8_t fail = 0xFF;
    CHECK(dot11b_bss_compatible(&bss, 6, &fail) && fail == DOT11B_FAIL_OK);
    CHECK(dot11b_bss_compatible(&bss, 0, NULL));
    CHECK(!dot11b_bss_compatible(&bss, 1, &fail) && fail == DOT11B_FAIL_BAD_CHANNEL);
    struct dot11_bss bad = bss;
    bad.capability |= DOT11_CAP_PRIVACY;
    CHECK(!dot11b_bss_compatible(&bad, 6, &fail) && fail == DOT11B_FAIL_SECURITY);
    bad = bss;
    bad.rates[0] = 0x0C;
    bad.rates[1] = 0x12;
    bad.rate_count = 2;
    CHECK(!dot11b_bss_compatible(&bad, 6, &fail) && fail == DOT11B_FAIL_NO_B_RATES);
    bad = bss;
    bad.rate_count = 2;
    bad.rates[0] = 0x82;
    bad.rates[1] = 0x8B;
    CHECK(!dot11b_bss_compatible(&bad, 6, &fail) && fail == DOT11B_FAIL_MISSING_BASIC);
    CHECK(!dot11b_bss_compatible(NULL, 6, &fail) && fail == DOT11B_FAIL_NO_B_RATES);
}
int main(void) {
    test_rates();
    test_chan();
    test_plcp();
    test_mgmt();
    if (failures) {
        fprintf(stderr, "dot11b tests: %d failure(s)\n", failures);
        return 1;
    }
    puts("dot11b tests passed");
    return 0;
}
