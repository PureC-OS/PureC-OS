#include "b_chan.h"

bool dot11b_channel_valid(uint8_t ch) {
    return ch >= DOT11B_CHAN_MIN && ch <= DOT11B_CHAN_MAX;
}

uint16_t dot11b_channel_to_freq_mhz(uint8_t ch) {
    if (!dot11b_channel_valid(ch))
        return 0;
    if (ch == 14u)
        return DOT11B_FREQ_14;
    return (uint16_t)(DOT11B_FREQ_1 + (uint16_t)(ch - 1u) * 5u);
}

uint8_t dot11b_freq_to_channel(uint16_t freq_mhz) {
    if (freq_mhz == DOT11B_FREQ_14)
        return 14u;
    if (freq_mhz < DOT11B_FREQ_1 || freq_mhz > 2472u)
        return 0;
    uint16_t off = (uint16_t)(freq_mhz - DOT11B_FREQ_1);
    if (off % 5u != 0)
        return 0;
    uint8_t ch = (uint8_t)(off / 5u + 1u);
    return dot11b_channel_valid(ch) ? ch : 0;
}

bool dot11b_channel_is_dsss_only(uint8_t ch) {
    return ch == 14u;
}
