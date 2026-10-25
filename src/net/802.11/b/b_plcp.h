#pragma once
#include <stdbool.h>
#include <stdint.h>
#define DOT11B_PREAMBLE_LONG_US 192u
#define DOT11B_PREAMBLE_SHORT_US 96u
#define DOT11B_SLOT_LONG_US 20u
#define DOT11B_SLOT_SHORT_US 9u
#define DOT11B_SIFS_US 10u
#define DOT11B_SIGNAL_1M 0x0Au
#define DOT11B_SIGNAL_2M 0x14u
#define DOT11B_SIGNAL_5_5M 0x37u
#define DOT11B_SIGNAL_11M 0x6Eu
bool dot11b_signal_valid(uint8_t signal);
uint8_t dot11b_signal_for_rate(uint8_t enc_rate);
uint8_t dot11b_rate_for_signal(uint8_t signal);
uint32_t dot11b_tx_time_us(uint16_t mpdu_bytes, uint8_t enc_rate, bool short_preamble);
bool dot11b_plcp_len_valid(uint16_t len_us, uint8_t signal);
