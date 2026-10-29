/*
 * AR9285 hardware layer, ported from the Linux ath9k driver
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
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ATH9K_RX_RING 8
#define ATH9K_TX_RING 4
#define ATH9K_RX_BUF_SIZE 2048
#define ATH9K_DESC_STRIDE 128
#define ATH9K_MAX_TX_LEN 1800
#define ATH9K_MAX_RATE_POWER 63
#define ATH9K_WAIT_TIMEOUT 100000
#define ATH9K_TIME_QUANTUM 10

#define ATH9K_EEP4K_NUM_2G_CAL_PIERS 3
#define ATH9K_EEP4K_NUM_CCK_TARGET 3
#define ATH9K_EEP4K_NUM_20_TARGET 3
#define ATH9K_EEP4K_NUM_40_TARGET 3
#define ATH9K_EEP4K_NUM_CTLS 12
#define ATH9K_EEP4K_NUM_BAND_EDGES 4
#define ATH9K_EEP4K_NUM_PD_GAINS 2
#define ATH9K_PD_GAIN_ICEPTS 5
#define ATH9K_NUM_PD_GAINS 4
#define ATH9K_PD_GAINS_IN_MASK 4
#define ATH9K_NUM_PDADC_VALUES 128
#define ATH9K_MAX_PWR_RANGE_IN_HALF_DB 64
#define ATH9K_EEP_MODAL_SPURS 5
#define ATH9K_BCHAN_UNUSED 0xFF
#define ATH9K_EEP_VER 0xE
#define ATH9K_EEP_NO_BACK_VER 0x1
#define ATH9K_EEP_MINOR_VER_2 0x2
#define ATH9K_EEP_MINOR_VER_3 0x3
#define ATH9K_PWR_TABLE_OFFSET_DB (-5)
#define ATH9K_EEPROM_MAGIC 0xa55a
#define ATH9K_EEPROM_MAGIC_OFFSET 0x0
#define ATH9K_EEPROM_OFFSET 0x2000
#define ATH9K_EEPROM_S 2
#define ATH9K_EEPMISC_BIG_ENDIAN 0x01
#define ATH9K_EEP_TXGAIN_HIGH_POWER 1
#define ATH9K_EEP_BB_DESIRED_SCALE_MASK 0x1f

enum ath9k_rates {
    ATH9K_RATE_6MB, ATH9K_RATE_9MB, ATH9K_RATE_12MB, ATH9K_RATE_18MB,
    ATH9K_RATE_24MB, ATH9K_RATE_36MB, ATH9K_RATE_48MB, ATH9K_RATE_54MB,
    ATH9K_RATE_XR, ATH9K_RATE_1L, ATH9K_RATE_2L, ATH9K_RATE_2S,
    ATH9K_RATE_5_5L, ATH9K_RATE_5_5S, ATH9K_RATE_11L, ATH9K_RATE_11S,
    ATH9K_RATE_DUP_CCK, ATH9K_RATE_DUP_OFDM, ATH9K_RATE_EXT_CCK,
    ATH9K_RATE_EXT_OFDM, ATH9K_RATE_HT20_0, ATH9K_RATE_HT20_1,
    ATH9K_RATE_HT20_2, ATH9K_RATE_HT20_3, ATH9K_RATE_HT20_4,
    ATH9K_RATE_HT20_5, ATH9K_RATE_HT20_6, ATH9K_RATE_HT20_7,
    ATH9K_RATE_HT40_0, ATH9K_RATE_HT40_1, ATH9K_RATE_HT40_2,
    ATH9K_RATE_HT40_3, ATH9K_RATE_HT40_4, ATH9K_RATE_HT40_5,
    ATH9K_RATE_HT40_6, ATH9K_RATE_HT40_7, ATH9K_RATE_COUNT
};

#define ATH9K_HWRATE_1M 0x1b
#define ATH9K_HWRATE_2M 0x1a
#define ATH9K_HWRATE_5_5M 0x19
#define ATH9K_HWRATE_11M 0x18
#define ATH9K_HWRATE_6M 0x0b
#define ATH9K_HWRATE_9M 0x0f
#define ATH9K_HWRATE_12M 0x0a
#define ATH9K_HWRATE_18M 0x0e
#define ATH9K_HWRATE_24M 0x09
#define ATH9K_HWRATE_36M 0x0d
#define ATH9K_HWRATE_48M 0x08
#define ATH9K_HWRATE_54M 0x0c

#define ATH9K_RX_FILTER_UCAST 0x00000001
#define ATH9K_RX_FILTER_MCAST 0x00000002
#define ATH9K_RX_FILTER_BCAST 0x00000004
#define ATH9K_RX_FILTER_CONTROL 0x00000008
#define ATH9K_RX_FILTER_BEACON 0x00000010
#define ATH9K_RX_FILTER_PROM 0x00000020
#define ATH9K_RX_FILTER_PROBEREQ 0x00000080

#define ATH9K_TXQ_USEDEFAULT 0xFFFFFFFFU

struct ath9k_spur_chan {
    uint16_t spur_chan;
    uint8_t spur_range_low;
    uint8_t spur_range_high;
} __attribute__((packed));

struct ath9k_base_eep_4k {
    uint16_t length;
    uint16_t checksum;
    uint16_t version;
    uint8_t op_cap_flags;
    uint8_t eep_misc;
    uint16_t reg_dmn[2];
    uint8_t mac_addr[6];
    uint8_t rx_mask;
    uint8_t tx_mask;
    uint16_t rf_silent;
    uint16_t bluetooth_options;
    uint16_t device_cap;
    uint32_t bin_build_number;
    uint8_t device_type;
    uint8_t tx_gain_type;
} __attribute__((packed));

struct ath9k_modal_eep_4k {
    uint32_t ant_ctrl_chain[1];
    uint32_t ant_ctrl_common;
    uint8_t antenna_gain_ch[1];
    uint8_t switch_settling;
    uint8_t tx_rx_atten_ch[1];
    uint8_t rx_tx_margin_ch[1];
    uint8_t adc_desired_size;
    uint8_t pga_desired_size;
    uint8_t xlna_gain_ch[1];
    uint8_t tx_end_to_xpa_off;
    uint8_t tx_end_to_rx_on;
    uint8_t tx_frame_to_xpa_on;
    uint8_t thresh62;
    uint8_t noise_floor_thresh_ch[1];
    uint8_t xpd_gain;
    uint8_t xpd;
    uint8_t iq_cal_i_ch[1];
    uint8_t iq_cal_q_ch[1];
    uint8_t pd_gain_overlap;
    uint8_t ob_0 : 4, ob_1 : 4;
    uint8_t db1_0 : 4, db1_1 : 4;
    uint8_t xpa_bias_lvl;
    uint8_t tx_frame_to_data_start;
    uint8_t tx_frame_to_pa_on;
    uint8_t ht40_power_inc_for_pdadc;
    uint8_t bsw_atten[1];
    uint8_t bsw_margin[1];
    uint8_t sw_settle_ht40;
    uint8_t xatten2_db[1];
    uint8_t xatten2_margin[1];
    uint8_t db2_0 : 4, db2_1 : 4;
    uint8_t version;
    uint8_t ob_2 : 4, ob_3 : 4;
    uint8_t ob_4 : 4, antdiv_ctl1 : 4;
    uint8_t db1_2 : 4, db1_3 : 4;
    uint8_t db1_4 : 4, antdiv_ctl2 : 4;
    uint8_t db2_2 : 4, db2_3 : 4;
    uint8_t db2_4 : 4, reserved : 4;
    uint8_t tx_diversity;
    uint8_t flc_pwr_thresh;
    uint8_t bb_scale_smrt_antenna;
    uint8_t future_modal[1];
    struct ath9k_spur_chan spur_chans[ATH9K_EEP_MODAL_SPURS];
} __attribute__((packed));

struct ath9k_cal_data_4k {
    uint8_t pwr_pdg[ATH9K_EEP4K_NUM_PD_GAINS][ATH9K_PD_GAIN_ICEPTS];
    uint8_t vpd_pdg[ATH9K_EEP4K_NUM_PD_GAINS][ATH9K_PD_GAIN_ICEPTS];
} __attribute__((packed));

struct ath9k_target_power_leg {
    uint8_t b_channel;
    uint8_t t_pow2x[4];
} __attribute__((packed));

struct ath9k_target_power_ht {
    uint8_t b_channel;
    uint8_t t_pow2x[8];
} __attribute__((packed));

struct ath9k_ctl_edge {
    uint8_t b_channel;
    uint8_t ctl;
} __attribute__((packed));

struct ath9k_ctl_data_4k {
    struct ath9k_ctl_edge edges[1][ATH9K_EEP4K_NUM_BAND_EDGES];
} __attribute__((packed));

struct ath9k_eeprom_4k {
    struct ath9k_base_eep_4k base;
    uint8_t cust_data[20];
    struct ath9k_modal_eep_4k modal;
    uint8_t cal_freq_pier_2g[ATH9K_EEP4K_NUM_2G_CAL_PIERS];
    struct ath9k_cal_data_4k cal_pier_data_2g[1][ATH9K_EEP4K_NUM_2G_CAL_PIERS];
    struct ath9k_target_power_leg cal_target_power_cck[ATH9K_EEP4K_NUM_CCK_TARGET];
    struct ath9k_target_power_leg cal_target_power_2g[ATH9K_EEP4K_NUM_20_TARGET];
    struct ath9k_target_power_ht cal_target_power_2g_ht20[ATH9K_EEP4K_NUM_20_TARGET];
    struct ath9k_target_power_ht cal_target_power_2g_ht40[ATH9K_EEP4K_NUM_40_TARGET];
    uint8_t ctl_index[ATH9K_EEP4K_NUM_CTLS];
    struct ath9k_ctl_data_4k ctl_data[ATH9K_EEP4K_NUM_CTLS];
    uint8_t padding;
} __attribute__((packed, aligned(2)));

struct ath9k_tx_desc_info {
    uint32_t desc_phys;
    uint8_t *buf;
    uint32_t buf_phys;
    uint16_t len;
    bool pending;
};

struct ath9k_rx_buf {
    volatile uint8_t *desc;
    uint32_t desc_phys;
    uint8_t *data;
    uint32_t data_phys;
};

struct ath9k_rx_frame {
    const uint8_t *data;
    uint16_t len;
    int8_t rssi;
    uint8_t rate;
    uint8_t status;
};

struct ath_hw {
    volatile uint8_t *regs;
    uint16_t mac_version;
    uint8_t mac_rev;
    bool is_pciexpress;
    uint32_t phy_rev;
    uint8_t radio_rev;
    struct ath9k_eeprom_4k eeprom;
    uint8_t mac_addr[6];
    uint8_t rx_chainmask;
    uint8_t tx_chainmask;
    uint16_t cur_freq;
    bool reset_power_on;
    bool chip_fullsleep;
    uint8_t power_mode;
    uint32_t clockrate;
    uint32_t slottime;
    uint32_t sta_id1_defaults;
    uint32_t tx_trig_level;
    uint32_t misc_mode;
    int8_t nf_nominal;
    int8_t nf_min;
    int8_t nf_max;
    uint8_t pacal_skipcount;
    uint8_t pacal_max_skipcount;
    int pacal_prev_offset;
    bool initialized;

    struct ath9k_rx_buf rx[ATH9K_RX_RING];
    uint8_t rx_fifo[ATH9K_RX_RING];
    uint8_t rx_head;
    uint8_t rx_count;
    int rx_hold;
    int rx_last;
    uint64_t rx_ring_phys;
    volatile uint8_t *rx_ring;
    bool rx_running;

    struct ath9k_tx_desc_info tx[ATH9K_TX_RING];
    volatile uint8_t *tx_ring;
    uint64_t tx_ring_phys;
    uint8_t tx_next;
    uint8_t tx_queue;
    uint32_t tx_ok;
    uint32_t tx_fail;
};

uint32_t ath9k_reg_read(struct ath_hw *ah, uint32_t reg);
void ath9k_reg_write(struct ath_hw *ah, uint32_t reg, uint32_t val);
void ath9k_udelay(uint32_t us);
void ath9k_log(int level, const char *fmt, ...);
uint64_t ath9k_dma_alloc_page(void **virt);
void ath9k_dma_free_page(uint64_t phys);

#define ATH9K_LOG_ERR 0
#define ATH9K_LOG_WARN 1
#define ATH9K_LOG_INFO 2
#define ATH9K_LOG_DBG 3

bool ath9k_hw_wait(struct ath_hw *ah, uint32_t reg, uint32_t mask,
                   uint32_t val, uint32_t timeout_us);
int ath9k_hw_init(struct ath_hw *ah, volatile uint8_t *regs);
int ath9k_hw_reset(struct ath_hw *ah, uint16_t freq);
bool ath9k_hw_disable(struct ath_hw *ah);
uint16_t ath9k_channel_to_freq(uint8_t channel);
void ath9k_hw_set_macaddr(struct ath_hw *ah, const uint8_t mac[6]);
void ath9k_hw_set_bssid(struct ath_hw *ah, const uint8_t bssid[6], uint16_t aid);
void ath9k_hw_set_rx_filter(struct ath_hw *ah, uint32_t bits);

int ath9k_hw_rx_start(struct ath_hw *ah);
void ath9k_hw_rx_stop(struct ath_hw *ah);
bool ath9k_hw_rx_next(struct ath_hw *ah, struct ath9k_rx_frame *frame);
void ath9k_hw_rx_release(struct ath_hw *ah);

int ath9k_hw_tx_init(struct ath_hw *ah);
void ath9k_hw_tx_release(struct ath_hw *ah);
bool ath9k_hw_tx_frame(struct ath_hw *ah, const uint8_t *frame, uint16_t len,
                       uint8_t hw_rate, bool no_ack);
void ath9k_hw_tx_reap(struct ath_hw *ah);

uint8_t ath9k_eeprom_ver(const struct ath_hw *ah);
uint8_t ath9k_eeprom_rev(const struct ath_hw *ah);
bool ath9k_eeprom_high_power(const struct ath_hw *ah);

int16_t ath9k_hw_interpolate(uint16_t target, uint16_t src_left,
                             uint16_t src_right, int16_t target_left,
                             int16_t target_right);
bool ath9k_hw_get_lower_upper_index(uint8_t target, const uint8_t *list,
                                    uint16_t list_size, uint16_t *index_l,
                                    uint16_t *index_r);
void ath9k_hw_fill_vpd_table(uint8_t pwr_min, uint8_t pwr_max,
                             const uint8_t *pwr_list, const uint8_t *vpd_list,
                             uint16_t num_intercepts, uint8_t *ret_vpd_list);
void ath9k_hw_get_delta_slope_vals(uint32_t coef_scaled, uint32_t *man,
                                   uint32_t *exp);
int ath9k_eeprom_check(struct ath_hw *ah);
