#pragma once
#include <stdbool.h>
#include <stdint.h>
#define DOT11B_CHAN_MIN 1u
#define DOT11B_CHAN_MAX 14u
#define DOT11B_FREQ_1 2412u
#define DOT11B_FREQ_14 2484u
bool dot11b_channel_valid(uint8_t ch);
uint16_t dot11b_channel_to_freq_mhz(uint8_t ch);
uint8_t dot11b_freq_to_channel(uint16_t freq_mhz);
bool dot11b_channel_is_dsss_only(uint8_t ch);
