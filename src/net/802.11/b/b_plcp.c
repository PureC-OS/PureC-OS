#include "b_plcp.h"
#include "b_rates.h"
bool dot11b_signal_valid(uint8_t signal) {
    return signal == DOT11B_SIGNAL_1M || signal == DOT11B_SIGNAL_2M || signal == DOT11B_SIGNAL_5_5M || signal == DOT11B_SIGNAL_11M;
}
uint8_t dot11b_signal_for_rate(uint8_t enc_rate) {
    switch (enc_rate & 0x7Fu) {
    case 0x02u:
        return DOT11B_SIGNAL_1M;
    case 0x04u:
        return DOT11B_SIGNAL_2M;
    case 0x0Bu:
        return DOT11B_SIGNAL_5_5M;
    case 0x16u:
        return DOT11B_SIGNAL_11M;
    default:
        return 0;
    }
}
uint8_t dot11b_rate_for_signal(uint8_t signal) {
    switch (signal) {
    case DOT11B_SIGNAL_1M:
        return DOT11B_RATE_1M;
    case DOT11B_SIGNAL_2M:
        return DOT11B_RATE_2M;
    case DOT11B_SIGNAL_5_5M:
        return DOT11B_RATE_5_5M;
    case DOT11B_SIGNAL_11M:
        return DOT11B_RATE_11M;
    default:
        return 0;
    }
}
uint32_t dot11b_tx_time_us(uint16_t mpdu_bytes, uint8_t enc_rate, bool short_preamble) {
    uint16_t x10 = dot11b_rate_mbps_x10(enc_rate);
    if (!x10)
        return 0;
    uint32_t preamble = short_preamble ? DOT11B_PREAMBLE_SHORT_US : DOT11B_PREAMBLE_LONG_US;
    uint32_t payload_us = ((uint32_t)mpdu_bytes * 80u + x10 / 2u) / x10;
    return preamble + payload_us;
}
bool dot11b_plcp_len_valid(uint16_t len_us, uint8_t signal) {
    if (!dot11b_signal_valid(signal) || !len_us)
        return false;
    uint32_t max_us = dot11b_tx_time_us(4095u, DOT11B_RATE_1M, false);
    return len_us <= max_us;
}
