/*
 * AR9285 4K EEPROM handling, ported from the Linux ath9k driver
 * Copyright (c) 2008-2011 Atheros Communications Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include "ath9k_priv.h"
#include <string.h>

#define EEP_WORDS (sizeof(struct ath9k_eeprom_4k) / sizeof(uint16_t))
#define SUB_NUM_CTL_MODES_AT_2G_40 3

static inline uint16_t min_u16(uint16_t a, uint16_t b) { return a < b ? a : b; }
static inline uint16_t max_u16(uint16_t a, uint16_t b) { return a > b ? a : b; }
static inline uint16_t swab16(uint16_t v) { return (uint16_t)((v >> 8) | (v << 8)); }

void ath9k_hw_analog_shift_rmw(struct ath_hw *ah, uint32_t reg, uint32_t mask,
                               uint32_t shift, uint32_t val)
{
    REG_RMW(ah, reg, ((val << shift) & mask), mask);
    ath9k_udelay(100);
}

int16_t ath9k_hw_interpolate(uint16_t target, uint16_t src_left,
                             uint16_t src_right, int16_t target_left,
                             int16_t target_right)
{
    if (src_right == src_left)
        return target_left;
    return (int16_t)(((target - src_left) * target_right +
                      (src_right - target) * target_left) /
                     (src_right - src_left));
}

bool ath9k_hw_get_lower_upper_index(uint8_t target, const uint8_t *list,
                                    uint16_t list_size, uint16_t *index_l,
                                    uint16_t *index_r)
{
    uint16_t i;

    if (target <= list[0]) {
        *index_l = *index_r = 0;
        return true;
    }
    if (target >= list[list_size - 1]) {
        *index_l = *index_r = (uint16_t)(list_size - 1);
        return true;
    }
    for (i = 0; i < list_size - 1; i++) {
        if (list[i] == target) {
            *index_l = *index_r = i;
            return true;
        }
        if (target < list[i + 1]) {
            *index_l = i;
            *index_r = (uint16_t)(i + 1);
            return false;
        }
    }
    return false;
}

void ath9k_hw_fill_vpd_table(uint8_t pwr_min, uint8_t pwr_max,
                             const uint8_t *pwr_list, const uint8_t *vpd_list,
                             uint16_t num_intercepts, uint8_t *ret_vpd_list)
{
    uint16_t i, k;
    uint8_t curr_pwr = pwr_min;
    uint16_t idx_l = 0, idx_r = 0;

    for (i = 0; i <= (pwr_max - pwr_min) / 2; i++) {
        ath9k_hw_get_lower_upper_index(curr_pwr, pwr_list, num_intercepts,
                                       &idx_l, &idx_r);
        if (idx_r < 1)
            idx_r = 1;
        if (idx_l == num_intercepts - 1)
            idx_l = (uint16_t)(num_intercepts - 2);
        if (pwr_list[idx_l] == pwr_list[idx_r])
            k = vpd_list[idx_l];
        else
            k = (uint16_t)(((curr_pwr - pwr_list[idx_l]) * vpd_list[idx_r] +
                            (pwr_list[idx_r] - curr_pwr) * vpd_list[idx_l]) /
                           (pwr_list[idx_r] - pwr_list[idx_l]));
        ret_vpd_list[i] = (uint8_t)k;
        curr_pwr = (uint8_t)(curr_pwr + 2);
    }
}

static bool nvram_read(struct ath_hw *ah, uint32_t off, uint16_t *data)
{
    (void)REG_READ(ah, ATH9K_EEPROM_OFFSET + (off << ATH9K_EEPROM_S));
    if (!ath9k_hw_wait(ah, AR_EEPROM_STATUS_DATA(ah),
                       AR_EEPROM_STATUS_DATA_BUSY | AR_EEPROM_STATUS_DATA_PROT_ACCESS,
                       0, ATH9K_WAIT_TIMEOUT))
        return false;
    *data = (uint16_t)MS(REG_READ(ah, AR_EEPROM_STATUS_DATA(ah)),
                         AR_EEPROM_STATUS_DATA_VAL);
    return true;
}

uint8_t ath9k_eeprom_ver(const struct ath_hw *ah)
{
    return (uint8_t)((ah->eeprom.base.version & 0xF000) >> 12);
}

uint8_t ath9k_eeprom_rev(const struct ath_hw *ah)
{
    return (uint8_t)(ah->eeprom.base.version & 0x0FFF);
}

bool ath9k_eeprom_high_power(const struct ath_hw *ah)
{
    return ah->eeprom.base.tx_gain_type == ATH9K_EEP_TXGAIN_HIGH_POWER;
}

uint16_t ath9k_eeprom_spur_channel(const struct ath_hw *ah, uint16_t i)
{
    return ah->eeprom.modal.spur_chans[i].spur_chan;
}

static bool nvram_swap_data(struct ath_hw *ah, bool *swap_needed)
{
    uint16_t magic;
    uint16_t *eepdata = (uint16_t *)&ah->eeprom;
    bool needs_byteswap = false;
    size_t i;

    if (!nvram_read(ah, ATH9K_EEPROM_MAGIC_OFFSET, &magic)) {
        ath9k_log(ATH9K_LOG_ERR, "ath9k: reading EEPROM magic failed");
        return false;
    }
    if (swab16(magic) == ATH9K_EEPROM_MAGIC) {
        needs_byteswap = true;
    } else if (magic != ATH9K_EEPROM_MAGIC) {
        ath9k_log(ATH9K_LOG_ERR, "ath9k: invalid EEPROM magic 0x%04x", magic);
        return false;
    }
    if (needs_byteswap)
        for (i = 0; i < EEP_WORDS; i++)
            eepdata[i] = swab16(eepdata[i]);

    *swap_needed = (ah->eeprom.base.eep_misc & ATH9K_EEPMISC_BIG_ENDIAN) != 0;
    return true;
}

int ath9k_eeprom_check(struct ath_hw *ah)
{
    uint16_t *eepdata = (uint16_t *)&ah->eeprom;
    bool big_endian = false;
    uint32_t sum = 0;
    size_t el, i;

    if (!nvram_swap_data(ah, &big_endian))
        return -1;
    if (big_endian) {
        ath9k_log(ATH9K_LOG_ERR, "ath9k: big-endian EEPROM image is not supported");
        return -1;
    }

    el = ah->eeprom.base.length / sizeof(uint16_t);
    if (el > EEP_WORDS)
        el = EEP_WORDS;
    for (i = 0; i < el; i++)
        sum ^= eepdata[i];
    if (sum != 0xffff) {
        ath9k_log(ATH9K_LOG_ERR, "ath9k: bad EEPROM checksum 0x%x", sum);
        return -1;
    }
    if (ath9k_eeprom_ver(ah) != ATH9K_EEP_VER ||
        ath9k_eeprom_rev(ah) < ATH9K_EEP_NO_BACK_VER) {
        ath9k_log(ATH9K_LOG_ERR, "ath9k: bad EEPROM ver 0x%04x rev 0x%04x",
                  ath9k_eeprom_ver(ah), ath9k_eeprom_rev(ah));
        return -1;
    }
    return 0;
}

int ath9k_eeprom_init(struct ath_hw *ah)
{
    uint16_t *eep_data = (uint16_t *)&ah->eeprom;
    size_t addr;

    for (addr = 0; addr < EEP_WORDS; addr++) {
        if (!nvram_read(ah, (uint32_t)(addr + 64), eep_data))
            return -1;
        eep_data++;
    }
    return ath9k_eeprom_check(ah);
}

static uint16_t fbin2freq(uint8_t fbin)
{
    return (uint16_t)(2300 + fbin);
}

static void get_legacy_target_powers(uint16_t freq,
                                     const struct ath9k_target_power_leg *pow_info,
                                     uint16_t num_channels,
                                     struct ath9k_target_power_leg *new_power,
                                     uint16_t num_rates)
{
    uint16_t clo, chi;
    int i;
    int match_index = -1, low_index = -1;

    if (freq <= fbin2freq(pow_info[0].b_channel)) {
        match_index = 0;
    } else {
        for (i = 0; i < num_channels && pow_info[i].b_channel != ATH9K_BCHAN_UNUSED; i++) {
            if (freq == fbin2freq(pow_info[i].b_channel)) {
                match_index = i;
                break;
            } else if (freq < fbin2freq(pow_info[i].b_channel) && i > 0 &&
                       freq > fbin2freq(pow_info[i - 1].b_channel)) {
                low_index = i - 1;
                break;
            }
        }
        if (match_index == -1 && low_index == -1)
            match_index = i - 1;
    }
    if (match_index != -1) {
        *new_power = pow_info[match_index];
    } else {
        clo = fbin2freq(pow_info[low_index].b_channel);
        chi = fbin2freq(pow_info[low_index + 1].b_channel);
        for (i = 0; i < num_rates; i++)
            new_power->t_pow2x[i] = (uint8_t)ath9k_hw_interpolate(
                freq, clo, chi, pow_info[low_index].t_pow2x[i],
                pow_info[low_index + 1].t_pow2x[i]);
    }
}

static void get_target_powers(uint16_t freq,
                              const struct ath9k_target_power_ht *pow_info,
                              uint16_t num_channels,
                              struct ath9k_target_power_ht *new_power,
                              uint16_t num_rates)
{
    uint16_t clo, chi;
    int i;
    int match_index = -1, low_index = -1;

    if (freq <= fbin2freq(pow_info[0].b_channel)) {
        match_index = 0;
    } else {
        for (i = 0; i < num_channels && pow_info[i].b_channel != ATH9K_BCHAN_UNUSED; i++) {
            if (freq == fbin2freq(pow_info[i].b_channel)) {
                match_index = i;
                break;
            } else if (freq < fbin2freq(pow_info[i].b_channel) && i > 0 &&
                       freq > fbin2freq(pow_info[i - 1].b_channel)) {
                low_index = i - 1;
                break;
            }
        }
        if (match_index == -1 && low_index == -1)
            match_index = i - 1;
    }
    if (match_index != -1) {
        *new_power = pow_info[match_index];
    } else {
        clo = fbin2freq(pow_info[low_index].b_channel);
        chi = fbin2freq(pow_info[low_index + 1].b_channel);
        for (i = 0; i < num_rates; i++)
            new_power->t_pow2x[i] = (uint8_t)ath9k_hw_interpolate(
                freq, clo, chi, pow_info[low_index].t_pow2x[i],
                pow_info[low_index + 1].t_pow2x[i]);
    }
}

static uint16_t get_max_edge_power(uint16_t freq,
                                   const struct ath9k_ctl_edge *edges,
                                   int num_band_edges)
{
    uint16_t twice_max_edge_power = ATH9K_MAX_RATE_POWER;
    int i;

    for (i = 0; i < num_band_edges && edges[i].b_channel != ATH9K_BCHAN_UNUSED; i++) {
        if (freq == fbin2freq(edges[i].b_channel)) {
            twice_max_edge_power = edges[i].ctl & 0x3f;
            break;
        } else if (i > 0 && freq < fbin2freq(edges[i].b_channel)) {
            if (fbin2freq(edges[i - 1].b_channel) < freq &&
                ((edges[i - 1].ctl >> 6) & 0x03))
                twice_max_edge_power = edges[i - 1].ctl & 0x3f;
            break;
        }
    }
    return twice_max_edge_power;
}

static void get_gain_boundaries_pdadcs(const struct ath_hw *ah, uint16_t freq,
                                       const struct ath9k_cal_data_4k *data,
                                       const uint8_t *b_chans, uint16_t avail_piers,
                                       uint16_t pd_gain_overlap,
                                       uint16_t *pd_gain_boundaries,
                                       uint8_t *pdadc_values, uint16_t num_xpd_gains)
{
    int i, j, k;
    int16_t ss;
    uint16_t idx_l = 0, idx_r = 0, num_piers;
    static uint8_t vpd_table_l[ATH9K_NUM_PD_GAINS][ATH9K_MAX_PWR_RANGE_IN_HALF_DB];
    static uint8_t vpd_table_r[ATH9K_NUM_PD_GAINS][ATH9K_MAX_PWR_RANGE_IN_HALF_DB];
    static uint8_t vpd_table_i[ATH9K_NUM_PD_GAINS][ATH9K_MAX_PWR_RANGE_IN_HALF_DB];
    const uint8_t *vpd_l, *vpd_r, *pwr_l, *pwr_r;
    uint8_t min_pwr_t4[ATH9K_NUM_PD_GAINS];
    uint8_t max_pwr_t4[ATH9K_NUM_PD_GAINS];
    int16_t vpd_step, tmp_val;
    uint16_t size_curr_vpd_table, max_index, tgt_index;
    bool match;
    uint8_t fbin;

    (void)ah;
    memset(min_pwr_t4, 0, sizeof(min_pwr_t4));
    memset(max_pwr_t4, 0, sizeof(max_pwr_t4));

    for (num_piers = 0; num_piers < avail_piers; num_piers++)
        if (b_chans[num_piers] == ATH9K_BCHAN_UNUSED)
            break;

    fbin = (uint8_t)(freq - 2300);
    match = ath9k_hw_get_lower_upper_index(fbin, b_chans, num_piers, &idx_l, &idx_r);

    if (match) {
        for (i = 0; i < num_xpd_gains; i++) {
            min_pwr_t4[i] = data[idx_l].pwr_pdg[i][0];
            max_pwr_t4[i] = data[idx_l].pwr_pdg[i][ATH9K_PD_GAIN_ICEPTS - 1];
            ath9k_hw_fill_vpd_table(min_pwr_t4[i], max_pwr_t4[i],
                                    data[idx_l].pwr_pdg[i], data[idx_l].vpd_pdg[i],
                                    ATH9K_PD_GAIN_ICEPTS, vpd_table_i[i]);
        }
    } else {
        for (i = 0; i < num_xpd_gains; i++) {
            vpd_l = data[idx_l].vpd_pdg[i];
            pwr_l = data[idx_l].pwr_pdg[i];
            vpd_r = data[idx_r].vpd_pdg[i];
            pwr_r = data[idx_r].pwr_pdg[i];

            min_pwr_t4[i] = (uint8_t)max_u16(pwr_l[0], pwr_r[0]);
            max_pwr_t4[i] = (uint8_t)min_u16(pwr_l[ATH9K_PD_GAIN_ICEPTS - 1],
                                             pwr_r[ATH9K_PD_GAIN_ICEPTS - 1]);

            ath9k_hw_fill_vpd_table(min_pwr_t4[i], max_pwr_t4[i], pwr_l, vpd_l,
                                    ATH9K_PD_GAIN_ICEPTS, vpd_table_l[i]);
            ath9k_hw_fill_vpd_table(min_pwr_t4[i], max_pwr_t4[i], pwr_r, vpd_r,
                                    ATH9K_PD_GAIN_ICEPTS, vpd_table_r[i]);

            for (j = 0; j <= (max_pwr_t4[i] - min_pwr_t4[i]) / 2; j++)
                vpd_table_i[i][j] = (uint8_t)ath9k_hw_interpolate(
                    fbin, b_chans[idx_l], b_chans[idx_r], vpd_table_l[i][j],
                    vpd_table_r[i][j]);
        }
    }

    k = 0;
    for (i = 0; i < num_xpd_gains; i++) {
        if (i == num_xpd_gains - 1)
            pd_gain_boundaries[i] = (uint16_t)(max_pwr_t4[i] / 2);
        else
            pd_gain_boundaries[i] = (uint16_t)((max_pwr_t4[i] + min_pwr_t4[i + 1]) / 4);

        pd_gain_boundaries[i] = min_u16(ATH9K_MAX_RATE_POWER, pd_gain_boundaries[i]);

        if (i == 0)
            ss = (int16_t)(0 - (min_pwr_t4[i] / 2));
        else
            ss = (int16_t)((pd_gain_boundaries[i - 1] - (min_pwr_t4[i] / 2)) -
                           pd_gain_overlap + 1);

        vpd_step = (int16_t)(vpd_table_i[i][1] - vpd_table_i[i][0]);
        vpd_step = (int16_t)(vpd_step < 1 ? 1 : vpd_step);

        while (ss < 0 && k < ATH9K_NUM_PDADC_VALUES - 1) {
            tmp_val = (int16_t)(vpd_table_i[i][0] + ss * vpd_step);
            pdadc_values[k++] = (uint8_t)(tmp_val < 0 ? 0 : tmp_val);
            ss++;
        }

        size_curr_vpd_table = (uint8_t)((max_pwr_t4[i] - min_pwr_t4[i]) / 2 + 1);
        tgt_index = (uint8_t)(pd_gain_boundaries[i] + pd_gain_overlap -
                              (min_pwr_t4[i] / 2));
        max_index = tgt_index < size_curr_vpd_table ? tgt_index : size_curr_vpd_table;

        while (ss < max_index && k < ATH9K_NUM_PDADC_VALUES - 1)
            pdadc_values[k++] = vpd_table_i[i][ss++];

        vpd_step = (int16_t)(vpd_table_i[i][size_curr_vpd_table - 1] -
                             vpd_table_i[i][size_curr_vpd_table - 2]);
        vpd_step = (int16_t)(vpd_step < 1 ? 1 : vpd_step);

        if (tgt_index >= max_index) {
            while (ss <= tgt_index && k < ATH9K_NUM_PDADC_VALUES - 1) {
                tmp_val = (int16_t)(vpd_table_i[i][size_curr_vpd_table - 1] +
                                    (ss - max_index + 1) * vpd_step);
                pdadc_values[k++] = (uint8_t)(tmp_val > 255 ? 255 : tmp_val);
                ss++;
            }
        }
    }

    while (i < ATH9K_PD_GAINS_IN_MASK) {
        pd_gain_boundaries[i] = 58;
        i++;
    }
    while (k < ATH9K_NUM_PDADC_VALUES) {
        pdadc_values[k] = pdadc_values[k - 1];
        k++;
    }
}

static void set_power_cal_table(struct ath_hw *ah, uint16_t freq)
{
    const struct ath9k_eeprom_4k *eep = &ah->eeprom;
    uint16_t pd_gain_overlap;
    static uint8_t pdadc_values[ATH9K_NUM_PDADC_VALUES];
    uint16_t gain_boundaries[ATH9K_PD_GAINS_IN_MASK];
    uint16_t i, j;
    uint16_t num_xpd_gain, xpd_mask;
    uint16_t xpd_gain_values[ATH9K_EEP4K_NUM_PD_GAINS] = {0, 0};
    uint32_t reg32, reg_offset;

    xpd_mask = eep->modal.xpd_gain;

    if (ath9k_eeprom_rev(ah) >= ATH9K_EEP_MINOR_VER_2)
        pd_gain_overlap = eep->modal.pd_gain_overlap;
    else
        pd_gain_overlap = (uint16_t)MS(REG_READ(ah, AR_PHY_TPCRG5),
                                       AR_PHY_TPCRG5_PD_GAIN_OVERLAP);

    num_xpd_gain = 0;
    for (i = 1; i <= ATH9K_PD_GAINS_IN_MASK; i++) {
        if ((xpd_mask >> (ATH9K_PD_GAINS_IN_MASK - i)) & 1) {
            if (num_xpd_gain >= ATH9K_EEP4K_NUM_PD_GAINS)
                break;
            xpd_gain_values[num_xpd_gain] = (uint16_t)(ATH9K_PD_GAINS_IN_MASK - i);
            num_xpd_gain++;
        }
    }
    if (!num_xpd_gain)
        return;

    REG_RMW_FIELD(ah, AR_PHY_TPCRG1, AR_PHY_TPCRG1_NUM_PD_GAIN, (num_xpd_gain - 1) & 0x3);
    REG_RMW_FIELD(ah, AR_PHY_TPCRG1, AR_PHY_TPCRG1_PD_GAIN_1, xpd_gain_values[0]);
    REG_RMW_FIELD(ah, AR_PHY_TPCRG1, AR_PHY_TPCRG1_PD_GAIN_2, xpd_gain_values[1]);
    REG_RMW_FIELD(ah, AR_PHY_TPCRG1, AR_PHY_TPCRG1_PD_GAIN_3, 0);

    if (!(eep->base.tx_mask & 1))
        return;

    get_gain_boundaries_pdadcs(ah, freq, eep->cal_pier_data_2g[0],
                               eep->cal_freq_pier_2g,
                               ATH9K_EEP4K_NUM_2G_CAL_PIERS, pd_gain_overlap,
                               gain_boundaries, pdadc_values, num_xpd_gain);

    REG_WRITE(ah, AR_PHY_TPCRG5,
              SM(pd_gain_overlap, AR_PHY_TPCRG5_PD_GAIN_OVERLAP) |
              SM(gain_boundaries[0], AR_PHY_TPCRG5_PD_GAIN_BOUNDARY_1) |
              SM(gain_boundaries[1], AR_PHY_TPCRG5_PD_GAIN_BOUNDARY_2) |
              SM(gain_boundaries[2], AR_PHY_TPCRG5_PD_GAIN_BOUNDARY_3) |
              SM(gain_boundaries[3], AR_PHY_TPCRG5_PD_GAIN_BOUNDARY_4));

    reg_offset = AR_PHY_BASE + (672 << 2);
    for (j = 0; j < 32; j++) {
        reg32 = (uint32_t)pdadc_values[4 * j] |
                ((uint32_t)pdadc_values[4 * j + 1] << 8) |
                ((uint32_t)pdadc_values[4 * j + 2] << 16) |
                ((uint32_t)pdadc_values[4 * j + 3] << 24);
        REG_WRITE(ah, reg_offset, reg32);
        reg_offset += 4;
    }
}

static void set_power_per_rate_table(struct ath_hw *ah, uint16_t freq,
                                     int16_t *rates, uint16_t cfg_ctl,
                                     uint16_t antenna_reduction,
                                     uint16_t power_limit)
{
    const struct ath9k_eeprom_4k *eep = &ah->eeprom;
    struct ath9k_target_power_leg target_cck = {0, {0, 0, 0, 0}};
    struct ath9k_target_power_leg target_ofdm = {0, {0, 0, 0, 0}};
    struct ath9k_target_power_ht target_ht20 = {0, {0, 0, 0, 0, 0, 0, 0, 0}};
    static const uint16_t ctl_modes[] = {ATH9K_CTL_11B, ATH9K_CTL_11G, ATH9K_CTL_2GHT20,
                                         ATH9K_CTL_11B | 0x8000, ATH9K_CTL_11G | 0x8000, 7};
    uint16_t scaled_power, min_ctl_power, twice_min_edge, twice_max_edge;
    uint16_t num_ctl_modes, ctl_mode;
    int i;

    scaled_power = power_limit > antenna_reduction ? power_limit - antenna_reduction : 0;
    scaled_power = min_u16(scaled_power, ATH9K_MAX_RATE_POWER);
    num_ctl_modes = (uint16_t)(sizeof(ctl_modes) / sizeof(ctl_modes[0]) -
                               SUB_NUM_CTL_MODES_AT_2G_40);

    get_legacy_target_powers(freq, eep->cal_target_power_cck, ATH9K_EEP4K_NUM_CCK_TARGET,
                             &target_cck, 4);
    get_legacy_target_powers(freq, eep->cal_target_power_2g, ATH9K_EEP4K_NUM_20_TARGET,
                             &target_ofdm, 4);
    get_target_powers(freq, eep->cal_target_power_2g_ht20, ATH9K_EEP4K_NUM_20_TARGET,
                      &target_ht20, 8);

    for (ctl_mode = 0; ctl_mode < num_ctl_modes; ctl_mode++) {
        uint16_t want = (uint16_t)((cfg_ctl & ~ATH9K_CTL_MODE_M) |
                                   (ctl_modes[ctl_mode] & ATH9K_CTL_MODE_M));
        twice_max_edge = ATH9K_MAX_RATE_POWER;

        for (i = 0; i < ATH9K_EEP4K_NUM_CTLS && eep->ctl_index[i]; i++) {
            bool hit = want == eep->ctl_index[i] ||
                       want == ((eep->ctl_index[i] & ATH9K_CTL_MODE_M) | ATH9K_SD_NO_CTL);
            if (!hit)
                continue;
            twice_min_edge = get_max_edge_power(freq, eep->ctl_data[i].edges[0],
                                                ATH9K_EEP4K_NUM_BAND_EDGES);
            if ((cfg_ctl & ~ATH9K_CTL_MODE_M) == ATH9K_SD_NO_CTL) {
                twice_max_edge = min_u16(twice_max_edge, twice_min_edge);
            } else {
                twice_max_edge = twice_min_edge;
                break;
            }
        }

        min_ctl_power = (uint8_t)min_u16(twice_max_edge, scaled_power);

        switch (ctl_modes[ctl_mode]) {
        case ATH9K_CTL_11B:
            for (i = 0; i < 4; i++)
                target_cck.t_pow2x[i] = (uint8_t)min_u16(target_cck.t_pow2x[i], min_ctl_power);
            break;
        case ATH9K_CTL_11G:
            for (i = 0; i < 4; i++)
                target_ofdm.t_pow2x[i] = (uint8_t)min_u16(target_ofdm.t_pow2x[i], min_ctl_power);
            break;
        case ATH9K_CTL_2GHT20:
            for (i = 0; i < 8; i++)
                target_ht20.t_pow2x[i] = (uint8_t)min_u16(target_ht20.t_pow2x[i], min_ctl_power);
            break;
        default:
            break;
        }
    }

    rates[ATH9K_RATE_6MB] = rates[ATH9K_RATE_9MB] = rates[ATH9K_RATE_12MB] =
        rates[ATH9K_RATE_18MB] = rates[ATH9K_RATE_24MB] = target_ofdm.t_pow2x[0];
    rates[ATH9K_RATE_36MB] = target_ofdm.t_pow2x[1];
    rates[ATH9K_RATE_48MB] = target_ofdm.t_pow2x[2];
    rates[ATH9K_RATE_54MB] = target_ofdm.t_pow2x[3];
    rates[ATH9K_RATE_XR] = target_ofdm.t_pow2x[0];

    for (i = 0; i < 8; i++)
        rates[ATH9K_RATE_HT20_0 + i] = target_ht20.t_pow2x[i];

    rates[ATH9K_RATE_1L] = target_cck.t_pow2x[0];
    rates[ATH9K_RATE_2S] = rates[ATH9K_RATE_2L] = target_cck.t_pow2x[1];
    rates[ATH9K_RATE_5_5S] = rates[ATH9K_RATE_5_5L] = target_cck.t_pow2x[2];
    rates[ATH9K_RATE_11S] = rates[ATH9K_RATE_11L] = target_cck.t_pow2x[3];
}

void ath9k_eeprom_set_txpower(struct ath_hw *ah, uint16_t freq, uint16_t cfg_ctl,
                              uint8_t antenna_reduction, uint8_t power_limit)
{
    int16_t rates[ATH9K_RATE_COUNT];
    int i;

    memset(rates, 0, sizeof(rates));
    set_power_per_rate_table(ah, freq, rates, cfg_ctl, antenna_reduction, power_limit);
    set_power_cal_table(ah, freq);

    for (i = 0; i < ATH9K_RATE_COUNT; i++) {
        if (rates[i] > ATH9K_MAX_RATE_POWER)
            rates[i] = ATH9K_MAX_RATE_POWER;
        rates[i] = (int16_t)(rates[i] - ATH9K_PWR_TABLE_OFFSET_DB * 2);
    }

    REG_WRITE(ah, AR_PHY_POWER_TX_RATE1,
              ATH9K_POW_SM(rates[ATH9K_RATE_18MB], 24) | ATH9K_POW_SM(rates[ATH9K_RATE_12MB], 16) |
              ATH9K_POW_SM(rates[ATH9K_RATE_9MB], 8) | ATH9K_POW_SM(rates[ATH9K_RATE_6MB], 0));
    REG_WRITE(ah, AR_PHY_POWER_TX_RATE2,
              ATH9K_POW_SM(rates[ATH9K_RATE_54MB], 24) | ATH9K_POW_SM(rates[ATH9K_RATE_48MB], 16) |
              ATH9K_POW_SM(rates[ATH9K_RATE_36MB], 8) | ATH9K_POW_SM(rates[ATH9K_RATE_24MB], 0));
    REG_WRITE(ah, AR_PHY_POWER_TX_RATE3,
              ATH9K_POW_SM(rates[ATH9K_RATE_2S], 24) | ATH9K_POW_SM(rates[ATH9K_RATE_2L], 16) |
              ATH9K_POW_SM(rates[ATH9K_RATE_XR], 8) | ATH9K_POW_SM(rates[ATH9K_RATE_1L], 0));
    REG_WRITE(ah, AR_PHY_POWER_TX_RATE4,
              ATH9K_POW_SM(rates[ATH9K_RATE_11S], 24) | ATH9K_POW_SM(rates[ATH9K_RATE_11L], 16) |
              ATH9K_POW_SM(rates[ATH9K_RATE_5_5S], 8) | ATH9K_POW_SM(rates[ATH9K_RATE_5_5L], 0));
    REG_WRITE(ah, AR_PHY_POWER_TX_RATE5,
              ATH9K_POW_SM(rates[ATH9K_RATE_HT20_3], 24) | ATH9K_POW_SM(rates[ATH9K_RATE_HT20_2], 16) |
              ATH9K_POW_SM(rates[ATH9K_RATE_HT20_1], 8) | ATH9K_POW_SM(rates[ATH9K_RATE_HT20_0], 0));
    REG_WRITE(ah, AR_PHY_POWER_TX_RATE6,
              ATH9K_POW_SM(rates[ATH9K_RATE_HT20_7], 24) | ATH9K_POW_SM(rates[ATH9K_RATE_HT20_6], 16) |
              ATH9K_POW_SM(rates[ATH9K_RATE_HT20_5], 8) | ATH9K_POW_SM(rates[ATH9K_RATE_HT20_4], 0));
    REG_WRITE(ah, AR_PHY_POWER_TX_RATE_MAX, ATH9K_MAX_RATE_POWER);
}

static void set_gain(struct ath_hw *ah, const struct ath9k_modal_eep_4k *modal,
                     uint8_t tx_rx_atten_local)
{
    REG_RMW(ah, AR_PHY_SWITCH_CHAIN_0, modal->ant_ctrl_chain[0], 0);

    REG_RMW(ah, AR_PHY_TIMING_CTRL4(0),
            SM(modal->iq_cal_i_ch[0], AR_PHY_TIMING_CTRL4_IQCORR_Q_I_COFF) |
            SM(modal->iq_cal_q_ch[0], AR_PHY_TIMING_CTRL4_IQCORR_Q_Q_COFF),
            AR_PHY_TIMING_CTRL4_IQCORR_Q_Q_COFF | AR_PHY_TIMING_CTRL4_IQCORR_Q_I_COFF);

    if (ath9k_eeprom_rev(ah) >= ATH9K_EEP_MINOR_VER_3) {
        tx_rx_atten_local = modal->tx_rx_atten_ch[0];

        REG_RMW_FIELD(ah, AR_PHY_GAIN_2GHZ, AR_PHY_GAIN_2GHZ_XATTEN1_MARGIN, modal->bsw_margin[0]);
        REG_RMW_FIELD(ah, AR_PHY_GAIN_2GHZ, AR_PHY_GAIN_2GHZ_XATTEN1_DB, modal->bsw_atten[0]);
        REG_RMW_FIELD(ah, AR_PHY_GAIN_2GHZ, AR_PHY_GAIN_2GHZ_XATTEN2_MARGIN, modal->xatten2_margin[0]);
        REG_RMW_FIELD(ah, AR_PHY_GAIN_2GHZ, AR_PHY_GAIN_2GHZ_XATTEN2_DB, modal->xatten2_db[0]);

        REG_RMW_FIELD(ah, AR_PHY_GAIN_2GHZ + 0x1000, AR_PHY_GAIN_2GHZ_XATTEN1_MARGIN, modal->bsw_margin[0]);
        REG_RMW_FIELD(ah, AR_PHY_GAIN_2GHZ + 0x1000, AR_PHY_GAIN_2GHZ_XATTEN1_DB, modal->bsw_atten[0]);
        REG_RMW_FIELD(ah, AR_PHY_GAIN_2GHZ + 0x1000, AR_PHY_GAIN_2GHZ_XATTEN2_MARGIN, modal->xatten2_margin[0]);
        REG_RMW_FIELD(ah, AR_PHY_GAIN_2GHZ + 0x1000, AR_PHY_GAIN_2GHZ_XATTEN2_DB, modal->xatten2_db[0]);
    }

    REG_RMW_FIELD(ah, AR_PHY_RXGAIN, AR9280_PHY_RXGAIN_TXRX_ATTEN, tx_rx_atten_local);
    REG_RMW_FIELD(ah, AR_PHY_RXGAIN, AR9280_PHY_RXGAIN_TXRX_MARGIN, modal->rx_tx_margin_ch[0]);
    REG_RMW_FIELD(ah, AR_PHY_RXGAIN + 0x1000, AR9280_PHY_RXGAIN_TXRX_ATTEN, tx_rx_atten_local);
    REG_RMW_FIELD(ah, AR_PHY_RXGAIN + 0x1000, AR9280_PHY_RXGAIN_TXRX_MARGIN, modal->rx_tx_margin_ch[0]);
}

#define AN_RMW(reg, fld, val) ath9k_hw_analog_shift_rmw(ah, (reg), (fld), fld##_S, (val))

void ath9k_eeprom_set_board_values(struct ath_hw *ah)
{
    const struct ath9k_modal_eep_4k *modal = &ah->eeprom.modal;
    const struct ath9k_base_eep_4k *base = &ah->eeprom.base;
    uint8_t ob[5], db1[5], db2[5];
    uint8_t bb_desired_scale;
    uint32_t reg_val;
    int i;

    REG_WRITE(ah, AR_PHY_SWITCH_COM, modal->ant_ctrl_common);
    set_gain(ah, modal, 23);

    if (modal->version >= 3) {
        uint8_t c1 = modal->antdiv_ctl1, c2 = modal->antdiv_ctl2;

        reg_val = REG_READ(ah, AR_PHY_MULTICHAIN_GAIN_CTL);
        reg_val &= ~(uint32_t)AR_PHY_9285_ANT_DIV_CTL_ALL;
        reg_val |= SM(c1, AR_PHY_9285_ANT_DIV_CTL);
        reg_val |= SM(c2, AR_PHY_9285_ANT_DIV_ALT_LNACONF);
        reg_val |= SM((c2 >> 2), AR_PHY_9285_ANT_DIV_MAIN_LNACONF);
        reg_val |= SM((c1 >> 1), AR_PHY_9285_ANT_DIV_ALT_GAINTB);
        reg_val |= SM((c1 >> 2), AR_PHY_9285_ANT_DIV_MAIN_GAINTB);
        REG_WRITE(ah, AR_PHY_MULTICHAIN_GAIN_CTL, reg_val);

        reg_val = REG_READ(ah, AR_PHY_CCK_DETECT);
        reg_val &= ~(uint32_t)AR_PHY_CCK_DETECT_BB_ENABLE_ANT_FAST_DIV;
        reg_val |= SM((c1 >> 3), AR_PHY_CCK_DETECT_BB_ENABLE_ANT_FAST_DIV);
        REG_WRITE(ah, AR_PHY_CCK_DETECT, reg_val);
    }

    if (modal->version >= 2) {
        ob[0] = modal->ob_0; ob[1] = modal->ob_1; ob[2] = modal->ob_2;
        ob[3] = modal->ob_3; ob[4] = modal->ob_4;
        db1[0] = modal->db1_0; db1[1] = modal->db1_1; db1[2] = modal->db1_2;
        db1[3] = modal->db1_3; db1[4] = modal->db1_4;
        db2[0] = modal->db2_0; db2[1] = modal->db2_1; db2[2] = modal->db2_2;
        db2[3] = modal->db2_3; db2[4] = modal->db2_4;
    } else if (modal->version == 1) {
        ob[0] = modal->ob_0;
        ob[1] = ob[2] = ob[3] = ob[4] = modal->ob_1;
        db1[0] = modal->db1_0;
        db1[1] = db1[2] = db1[3] = db1[4] = modal->db1_1;
        db2[0] = modal->db2_0;
        db2[1] = db2[2] = db2[3] = db2[4] = modal->db2_1;
    } else {
        for (i = 0; i < 5; i++) {
            ob[i] = modal->ob_0;
            db1[i] = modal->db1_0;
            db2[i] = modal->db1_0;
        }
    }

    AN_RMW(AR9285_AN_RF2G3, AR9285_AN_RF2G3_OB_0, ob[0]);
    AN_RMW(AR9285_AN_RF2G3, AR9285_AN_RF2G3_OB_1, ob[1]);
    AN_RMW(AR9285_AN_RF2G3, AR9285_AN_RF2G3_OB_2, ob[2]);
    AN_RMW(AR9285_AN_RF2G3, AR9285_AN_RF2G3_OB_3, ob[3]);
    AN_RMW(AR9285_AN_RF2G3, AR9285_AN_RF2G3_OB_4, ob[4]);
    AN_RMW(AR9285_AN_RF2G3, AR9285_AN_RF2G3_DB1_0, db1[0]);
    AN_RMW(AR9285_AN_RF2G3, AR9285_AN_RF2G3_DB1_1, db1[1]);
    AN_RMW(AR9285_AN_RF2G3, AR9285_AN_RF2G3_DB1_2, db1[2]);
    AN_RMW(AR9285_AN_RF2G4, AR9285_AN_RF2G4_DB1_3, db1[3]);
    AN_RMW(AR9285_AN_RF2G4, AR9285_AN_RF2G4_DB1_4, db1[4]);
    AN_RMW(AR9285_AN_RF2G4, AR9285_AN_RF2G4_DB2_0, db2[0]);
    AN_RMW(AR9285_AN_RF2G4, AR9285_AN_RF2G4_DB2_1, db2[1]);
    AN_RMW(AR9285_AN_RF2G4, AR9285_AN_RF2G4_DB2_2, db2[2]);
    AN_RMW(AR9285_AN_RF2G4, AR9285_AN_RF2G4_DB2_3, db2[3]);
    AN_RMW(AR9285_AN_RF2G4, AR9285_AN_RF2G4_DB2_4, db2[4]);

    REG_RMW_FIELD(ah, AR_PHY_SETTLING, AR_PHY_SETTLING_SWITCH, modal->switch_settling);
    REG_RMW_FIELD(ah, AR_PHY_DESIRED_SZ, AR_PHY_DESIRED_SZ_ADC, modal->adc_desired_size);

    REG_RMW(ah, AR_PHY_RF_CTL4,
            SM(modal->tx_end_to_xpa_off, AR_PHY_RF_CTL4_TX_END_XPAA_OFF) |
            SM(modal->tx_end_to_xpa_off, AR_PHY_RF_CTL4_TX_END_XPAB_OFF) |
            SM(modal->tx_frame_to_xpa_on, AR_PHY_RF_CTL4_FRAME_XPAA_ON) |
            SM(modal->tx_frame_to_xpa_on, AR_PHY_RF_CTL4_FRAME_XPAB_ON), 0);

    REG_RMW_FIELD(ah, AR_PHY_RF_CTL3, AR_PHY_TX_END_TO_A2_RX_ON, modal->tx_end_to_rx_on);
    REG_RMW_FIELD(ah, AR_PHY_CCA, AR9280_PHY_CCA_THRESH62, modal->thresh62);
    REG_RMW_FIELD(ah, AR_PHY_EXT_CCA0, AR_PHY_EXT_CCA0_THRESH62, modal->thresh62);

    if (ath9k_eeprom_rev(ah) >= ATH9K_EEP_MINOR_VER_2) {
        REG_RMW_FIELD(ah, AR_PHY_RF_CTL2, AR_PHY_TX_END_DATA_START, modal->tx_frame_to_data_start);
        REG_RMW_FIELD(ah, AR_PHY_RF_CTL2, AR_PHY_TX_END_PA_ON, modal->tx_frame_to_pa_on);
    }

    bb_desired_scale = modal->bb_scale_smrt_antenna & ATH9K_EEP_BB_DESIRED_SCALE_MASK;
    if (base->tx_gain_type == 0 && bb_desired_scale != 0) {
        uint32_t pwrctrl, mask, clr;

        mask = BIT(0) | BIT(5) | BIT(10) | BIT(15) | BIT(20) | BIT(25);
        pwrctrl = mask * bb_desired_scale;
        clr = mask * 0x1f;
        REG_RMW(ah, AR_PHY_TX_PWRCTRL8, pwrctrl, clr);
        REG_RMW(ah, AR_PHY_TX_PWRCTRL10, pwrctrl, clr);
        REG_RMW(ah, AR_PHY_CH0_TX_PWRCTRL12, pwrctrl, clr);

        mask = BIT(0) | BIT(5) | BIT(15);
        pwrctrl = mask * bb_desired_scale;
        clr = mask * 0x1f;
        REG_RMW(ah, AR_PHY_TX_PWRCTRL9, pwrctrl, clr);

        mask = BIT(0) | BIT(5);
        pwrctrl = mask * bb_desired_scale;
        clr = mask * 0x1f;
        REG_RMW(ah, AR_PHY_CH0_TX_PWRCTRL11, pwrctrl, clr);
        REG_RMW(ah, AR_PHY_CH0_TX_PWRCTRL13, pwrctrl, clr);
    }
}
