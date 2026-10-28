#pragma once

#include "ath9k_hw.h"
#include "ath9k_regs.h"

#define REG_READ(ah, r) ath9k_reg_read((ah), (r))
#define REG_WRITE(ah, r, v) ath9k_reg_write((ah), (r), (v))
#define REG_RMW(ah, r, set, clr) \
    ath9k_reg_write((ah), (r), (ath9k_reg_read((ah), (r)) & ~(uint32_t)(clr)) | (uint32_t)(set))
#define REG_RMW_FIELD(ah, r, f, v) \
    REG_RMW((ah), (r), (((uint32_t)(v) << f##_S) & (f)), (f))
#define REG_SET_BIT(ah, r, f) REG_RMW((ah), (r), (f), 0)
#define REG_CLR_BIT(ah, r, f) REG_RMW((ah), (r), 0, (f))

#define ATH9K_POW_SM(_r, _s) (((_r) & 0x3f) << (_s))
#define ATH9K_CTL_MODE_M 0xf
#define ATH9K_CTL_11B 1
#define ATH9K_CTL_11G 2
#define ATH9K_CTL_2GHT20 5
#define ATH9K_SD_NO_CTL 0xE0
#define ATH9K_PM_AWAKE 0
#define ATH9K_PM_FULL_SLEEP 1
#define ATH9K_PM_UNDEFINED 3
#define ATH9K_RESET_POWER_ON 0
#define ATH9K_RESET_WARM 1
#define ATH9K_RESET_COLD 2

int ath9k_eeprom_init(struct ath_hw *ah);
void ath9k_eeprom_set_board_values(struct ath_hw *ah);
void ath9k_eeprom_set_txpower(struct ath_hw *ah, uint16_t freq, uint16_t cfg_ctl,
                              uint8_t antenna_reduction, uint8_t power_limit);
uint16_t ath9k_eeprom_spur_channel(const struct ath_hw *ah, uint16_t i);
void ath9k_hw_analog_shift_rmw(struct ath_hw *ah, uint32_t reg, uint32_t mask,
                               uint32_t shift, uint32_t val);
