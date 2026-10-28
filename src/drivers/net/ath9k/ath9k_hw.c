/*
 * AR9285 hardware core, ported from the Linux ath9k driver
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
#include "ath9k_initvals.h"
#ifdef PUREC_HOST_TEST
#include <string.h>
#else
#include "lib/string.h"
#endif

#define POWER_UP_TIME 10000
#define BASE_ACTIVATE_DELAY 100
#define RTC_PLL_SETTLE_DELAY 100
#define COEF_SCALE_S 24
#define CLOCK_RATE_2GHZ_OFDM 44
#define AR9285_CLCAL_REDO_THRESH 1
#define AR9285_WA_DEFAULT_VALUE 0x004a050b
#define INIT_RSSI_THR 0x00000700
#define AR_DESC_STRIDE ATH9K_DESC_STRIDE

#ifdef PUREC_HOST_TEST
void (*ath9k_test_read_hook)(struct ath_hw *ah, uint32_t reg);
void (*ath9k_test_write_hook)(struct ath_hw *ah, uint32_t reg, uint32_t val);
#endif

uint32_t ath9k_reg_read(struct ath_hw *ah, uint32_t reg)
{
#ifdef PUREC_HOST_TEST
    if (ath9k_test_read_hook)
        ath9k_test_read_hook(ah, reg);
#endif
    return *(volatile uint32_t *)(ah->regs + reg);
}

void ath9k_reg_write(struct ath_hw *ah, uint32_t reg, uint32_t val)
{
    *(volatile uint32_t *)(ah->regs + reg) = val;
#ifdef PUREC_HOST_TEST
    if (ath9k_test_write_hook)
        ath9k_test_write_hook(ah, reg, val);
#endif
}

bool ath9k_hw_wait(struct ath_hw *ah, uint32_t reg, uint32_t mask,
                   uint32_t val, uint32_t timeout_us)
{
    uint32_t i;

    for (i = 0; i < timeout_us / ATH9K_TIME_QUANTUM; i++) {
        if ((REG_READ(ah, reg) & mask) == val)
            return true;
        ath9k_udelay(ATH9K_TIME_QUANTUM);
    }
    ath9k_log(ATH9K_LOG_DBG, "ath9k: timeout on reg 0x%x: 0x%08x & 0x%08x != 0x%08x",
              reg, REG_READ(ah, reg), mask, val);
    return false;
}

uint16_t ath9k_channel_to_freq(uint8_t channel)
{
    if (channel == 14)
        return 2484;
    if (channel >= 1 && channel <= 13)
        return (uint16_t)(2407 + 5 * channel);
    return 0;
}

static bool read_revisions(struct ath_hw *ah)
{
    uint32_t srev = REG_READ(ah, AR_SREV(ah));
    uint32_t val;

    if (srev == 0xFFFFFFFFU)
        return false;

    val = srev & AR_SREV_ID(ah);
    if (val == 0xFF) {
        val = srev;
        ah->mac_version = (uint16_t)((val & AR_SREV_VERSION2) >> AR_SREV_TYPE2_S);
        ah->mac_rev = (uint8_t)MS(val, AR_SREV_REVISION2);
        ah->is_pciexpress = (val & AR_SREV_TYPE2_HOST_MODE) ? false : true;
    } else {
        ah->mac_version = (uint16_t)MS(val, AR_SREV_VERSION);
        ah->mac_rev = (uint8_t)(val & AR_SREV_REVISION);
        ah->is_pciexpress = ah->mac_version == AR_SREV_VERSION_5416_PCIE;
    }
    return true;
}

static bool chip_test(struct ath_hw *ah)
{
    static const uint32_t pattern[4] = {0x55555555, 0xaaaaaaaa, 0x66666666, 0x99999999};
    uint32_t regs[2] = {AR_STA_ID0, AR_PHY_BASE + (8 << 2)};
    int i, j;

    for (i = 0; i < 2; i++) {
        uint32_t addr = regs[i];
        uint32_t hold = REG_READ(ah, addr);

        for (j = 0; j < 0x100; j++) {
            uint32_t wr = ((uint32_t)j << 16) | (uint32_t)j;

            REG_WRITE(ah, addr, wr);
            if (REG_READ(ah, addr) != wr) {
                ath9k_log(ATH9K_LOG_ERR, "ath9k: address test failed addr 0x%08x wr 0x%08x rd 0x%08x",
                          addr, wr, REG_READ(ah, addr));
                return false;
            }
        }
        for (j = 0; j < 4; j++) {
            REG_WRITE(ah, addr, pattern[j]);
            if (REG_READ(ah, addr) != pattern[j]) {
                ath9k_log(ATH9K_LOG_ERR, "ath9k: pattern test failed addr 0x%08x", addr);
                return false;
            }
        }
        REG_WRITE(ah, addr, hold);
    }
    ath9k_udelay(100);
    return true;
}

static bool set_reset(struct ath_hw *ah, int type)
{
    uint32_t rst_flags;
    uint32_t tmp;

    REG_WRITE(ah, AR_RTC_FORCE_WAKE(ah), AR_RTC_FORCE_WAKE_EN | AR_RTC_FORCE_WAKE_ON_INT);

    tmp = REG_READ(ah, AR_INTR_SYNC_CAUSE(ah));
    tmp &= AR_INTR_SYNC_LOCAL_TIMEOUT | AR_INTR_SYNC_RADM_CPL_TIMEOUT;
    if (tmp) {
        REG_WRITE(ah, AR_INTR_SYNC_ENABLE(ah), 0);
        REG_WRITE(ah, AR_RC, AR_RC_HOSTIF | AR_RC_AHB);
    } else {
        REG_WRITE(ah, AR_RC, AR_RC_AHB);
    }

    rst_flags = AR_RTC_RC_MAC_WARM;
    if (type == ATH9K_RESET_COLD)
        rst_flags |= AR_RTC_RC_MAC_COLD;

    REG_WRITE(ah, AR_RTC_RC(ah), rst_flags);
    ath9k_udelay(100);
    REG_WRITE(ah, AR_RTC_RC(ah), 0);
    if (!ath9k_hw_wait(ah, AR_RTC_RC(ah), AR_RTC_RC_M, 0, ATH9K_WAIT_TIMEOUT)) {
        ath9k_log(ATH9K_LOG_ERR, "ath9k: RTC stuck in MAC reset");
        return false;
    }
    REG_WRITE(ah, AR_RC, 0);
    return true;
}

static bool set_reset_power_on(struct ath_hw *ah)
{
    REG_WRITE(ah, AR_RTC_FORCE_WAKE(ah), AR_RTC_FORCE_WAKE_EN | AR_RTC_FORCE_WAKE_ON_INT);
    REG_WRITE(ah, AR_RC, AR_RC_AHB);
    REG_WRITE(ah, AR_RTC_RESET(ah), 0);
    ath9k_udelay(2);
    REG_WRITE(ah, AR_RC, 0);
    REG_WRITE(ah, AR_RTC_RESET(ah), 1);

    if (!ath9k_hw_wait(ah, AR_RTC_STATUS(ah), AR_RTC_STATUS_M(ah), AR_RTC_STATUS_ON,
                       ATH9K_WAIT_TIMEOUT)) {
        ath9k_log(ATH9K_LOG_ERR, "ath9k: RTC not waking up");
        return false;
    }
    return set_reset(ah, ATH9K_RESET_WARM);
}

static bool set_reset_reg(struct ath_hw *ah, int type)
{
    bool ret = false;

    REG_WRITE(ah, AR_RTC_FORCE_WAKE(ah), AR_RTC_FORCE_WAKE_EN | AR_RTC_FORCE_WAKE_ON_INT);

    if (!ah->reset_power_on)
        type = ATH9K_RESET_POWER_ON;

    switch (type) {
    case ATH9K_RESET_POWER_ON:
        ret = set_reset_power_on(ah);
        if (ret)
            ah->reset_power_on = true;
        break;
    case ATH9K_RESET_WARM:
    case ATH9K_RESET_COLD:
        ret = set_reset(ah, type);
        break;
    default:
        break;
    }
    return ret;
}

static void init_pll(struct ath_hw *ah)
{
    uint32_t pll = SM(5, AR_RTC_9160_PLL_REFDIV) | SM(0x2c, AR_RTC_9160_PLL_DIV);

    REG_WRITE(ah, AR_RTC_PLL_CONTROL(ah), pll);
    ath9k_udelay(RTC_PLL_SETTLE_DELAY);
    REG_WRITE(ah, AR_RTC_SLEEP_CLK(ah), AR_RTC_FORCE_DERIVED_CLK);
}

static bool set_power_awake(struct ath_hw *ah)
{
    uint32_t val;
    int i;

    if ((REG_READ(ah, AR_RTC_STATUS(ah)) & AR_RTC_STATUS_M(ah)) == AR_RTC_STATUS_SHUTDOWN) {
        if (!set_reset_reg(ah, ATH9K_RESET_POWER_ON))
            return false;
        init_pll(ah);
    }

    REG_SET_BIT(ah, AR_RTC_FORCE_WAKE(ah), AR_RTC_FORCE_WAKE_EN);
    ath9k_udelay(50);

    for (i = POWER_UP_TIME / 50; i > 0; i--) {
        val = REG_READ(ah, AR_RTC_STATUS(ah)) & AR_RTC_STATUS_M(ah);
        if (val == AR_RTC_STATUS_ON)
            break;
        ath9k_udelay(50);
        REG_SET_BIT(ah, AR_RTC_FORCE_WAKE(ah), AR_RTC_FORCE_WAKE_EN);
    }
    if (i == 0) {
        ath9k_log(ATH9K_LOG_ERR, "ath9k: failed to wake up in %uus", POWER_UP_TIME / 20);
        return false;
    }
    REG_CLR_BIT(ah, AR_STA_ID1, AR_STA_ID1_PWR_SAV);
    return true;
}

static void set_power_sleep(struct ath_hw *ah)
{
    REG_SET_BIT(ah, AR_STA_ID1, AR_STA_ID1_PWR_SAV);
    REG_CLR_BIT(ah, AR_RTC_FORCE_WAKE(ah), AR_RTC_FORCE_WAKE_EN);
    REG_WRITE(ah, AR_RC, AR_RC_AHB | AR_RC_HOSTIF);
    REG_CLR_BIT(ah, AR_RTC_RESET(ah), AR_RTC_RESET_EN);
    ath9k_udelay(2);
}

static bool set_power(struct ath_hw *ah, int mode)
{
    bool status = true;

    if (ah->power_mode == mode)
        return true;
    if (mode == ATH9K_PM_AWAKE) {
        status = set_power_awake(ah);
    } else {
        set_power_sleep(ah);
        ah->chip_fullsleep = true;
    }
    ah->power_mode = (uint8_t)mode;
    return status;
}

static void config_pci_powersave(struct ath_hw *ah)
{
    uint32_t val;
    size_t i;

    for (i = 0; i < AR9280PCIEPHY_CLKREQ_ALWAYS_ON_L1_9280_ROWS; i++)
        REG_WRITE(ah, ar9280PciePhy_clkreq_always_on_L1_9280[i][0],
                  ar9280PciePhy_clkreq_always_on_L1_9280[i][1]);
    ath9k_udelay(1000);

    val = AR9285_WA_DEFAULT_VALUE;
    val &= ~(uint32_t)AR_WA_D3_L1_DISABLE;
    val |= AR_WA_BIT6 | AR_WA_BIT7;
    REG_WRITE(ah, AR_WA(ah), val);
    REG_SET_BIT(ah, AR_PCIE_PM_CTRL(ah), AR_PCIE_PM_CTRL_ENA);
}

static bool rf_claim(struct ath_hw *ah)
{
    uint32_t val;
    int i;

    REG_WRITE(ah, AR_PHY(0), 0x00000007);
    REG_WRITE(ah, AR_PHY(0x36), 0x00007058);
    for (i = 0; i < 8; i++)
        REG_WRITE(ah, AR_PHY(0x20), 0x00010000);

    val = (REG_READ(ah, AR_PHY(256)) >> 24) & 0xff;
    val = ((val & 0xf0) >> 4) | ((val & 0x0f) << 4);
    val = ((val & 0xaa) >> 1) | ((val & 0x55) << 1);
    val = ((val & 0xcc) >> 2) | ((val & 0x33) << 2);
    val = ((val & 0xf0) >> 4) | ((val & 0x0f) << 4);
    ah->radio_rev = (uint8_t)val;

    switch (val & AR_RADIO_SREV_MAJOR) {
    case 0:
    case AR_RAD5133_SREV_MAJOR:
    case AR_RAD5122_SREV_MAJOR:
    case AR_RAD2133_SREV_MAJOR:
    case AR_RAD2122_SREV_MAJOR:
        return true;
    default:
        ath9k_log(ATH9K_LOG_ERR, "ath9k: radio chip rev 0x%02X not supported", val & AR_RADIO_SREV_MAJOR);
        return false;
    }
}

int ath9k_hw_init(struct ath_hw *ah, volatile uint8_t *regs)
{
    memset(ah, 0, sizeof(*ah));
    ah->regs = regs;
    ah->power_mode = ATH9K_PM_UNDEFINED;
    ah->rx_chainmask = 1;
    ah->tx_chainmask = 1;
    ah->slottime = 9;
    ah->sta_id1_defaults = AR_STA_ID1_CRPT_MIC_ENABLE | AR_STA_ID1_MCAST_KSRCH;
    ah->tx_trig_level = AR_FTRIG_256B >> AR_FTRIG_S;
    ah->nf_nominal = AR_PHY_CCA_NOM_VAL_9280_2GHZ;
    ah->nf_min = AR_PHY_CCA_MIN_GOOD_VAL_9280_2GHZ;
    ah->nf_max = AR_PHY_CCA_MAX_GOOD_VAL_9280_2GHZ;
    ah->rx_hold = -1;
    ah->rx_last = -1;

    if (!read_revisions(ah)) {
        ath9k_log(ATH9K_LOG_ERR, "ath9k: could not read hardware revisions");
        return -1;
    }
    ath9k_log(ATH9K_LOG_INFO, "ath9k: SREV version=0x%x rev=%u pcie=%d",
              ah->mac_version, ah->mac_rev, ah->is_pciexpress);
    if (ah->mac_version != AR_SREV_VERSION_9285) {
        ath9k_log(ATH9K_LOG_ERR, "ath9k: mac chip rev 0x%02x.%x is not AR9285",
                  ah->mac_version, ah->mac_rev);
        return -1;
    }
    if (ah->mac_rev < AR_SREV_REVISION_9285_12)
        ath9k_log(ATH9K_LOG_WARN, "ath9k: AR9285 rev %u is older than 1.2, init tables may be wrong",
                  ah->mac_rev);

    if (!set_reset_reg(ah, ATH9K_RESET_POWER_ON)) {
        ath9k_log(ATH9K_LOG_ERR, "ath9k: couldn't reset chip");
        return -1;
    }
    if (!set_power(ah, ATH9K_PM_AWAKE)) {
        ath9k_log(ATH9K_LOG_ERR, "ath9k: couldn't wake up chip");
        return -1;
    }

    ah->phy_rev = REG_READ(ah, AR_PHY_CHIP_ID);

    if (!chip_test(ah))
        return -1;
    if (!rf_claim(ah))
        return -1;
    if (ath9k_eeprom_init(ah) != 0)
        return -1;

    ath9k_log(ATH9K_LOG_INFO, "ath9k: EEPROM ver %u rev %u tx_gain_type %u",
              ath9k_eeprom_ver(ah), ath9k_eeprom_rev(ah), ah->eeprom.base.tx_gain_type);

    memcpy(ah->mac_addr, ah->eeprom.base.mac_addr, 6);
    if (!(ah->mac_addr[0] | ah->mac_addr[1] | ah->mac_addr[2] | ah->mac_addr[3] |
          ah->mac_addr[4] | ah->mac_addr[5]) ||
        (ah->mac_addr[0] & 1)) {
        ath9k_log(ATH9K_LOG_WARN, "ath9k: EEPROM has invalid MAC, using local address");
        ah->mac_addr[0] = 0x02;
        ah->mac_addr[1] = 0x50;
        ah->mac_addr[2] = 0x43;
        ah->mac_addr[3] = 0x4f;
        ah->mac_addr[4] = 0x53;
        ah->mac_addr[5] = (uint8_t)(REG_READ(ah, AR_TSF_L32) & 0xff);
    }

    config_pci_powersave(ah);
    ah->initialized = true;
    return 0;
}

static void set_clockrate(struct ath_hw *ah)
{
    ah->clockrate = CLOCK_RATE_2GHZ_OFDM;
}

static uint32_t mac_to_clks(struct ath_hw *ah, uint32_t usecs)
{
    return usecs * ah->clockrate;
}

static void init_global_settings(struct ath_hw *ah)
{
    int acktimeout, ctstimeout;
    int slottime = (int)ah->slottime;
    int sifstime = 10;
    uint32_t reg = REG_READ(ah, AR_USEC);
    int eifs = (int)(REG_READ(ah, AR_D_GBL_IFS_EIFS) / ah->clockrate);
    int rx_lat = (int)MS(reg, AR_USEC_RX_LAT);
    int tx_lat = (int)MS(reg, AR_USEC_TX_LAT);
    uint32_t v;

    if (ah->misc_mode)
        REG_SET_BIT(ah, AR_PCU_MISC, ah->misc_mode);

    acktimeout = slottime + sifstime;
    ctstimeout = acktimeout;
    acktimeout += 64 - sifstime - (int)ah->slottime;
    ctstimeout += 48 - sifstime - (int)ah->slottime;

    v = mac_to_clks(ah, (uint32_t)(sifstime - 2));
    REG_WRITE(ah, AR_D_GBL_IFS_SIFS, v > 0xFFFF ? 0xFFFF : v);
    v = mac_to_clks(ah, (uint32_t)slottime);
    REG_WRITE(ah, AR_D_GBL_IFS_SLOT, v > 0xFFFF ? 0xFFFF : v);
    v = mac_to_clks(ah, (uint32_t)acktimeout);
    REG_RMW_FIELD(ah, AR_TIME_OUT, AR_TIME_OUT_ACK, v);
    v = mac_to_clks(ah, (uint32_t)ctstimeout);
    REG_RMW_FIELD(ah, AR_TIME_OUT, AR_TIME_OUT_CTS, v);

    REG_WRITE(ah, AR_D_GBL_IFS_EIFS, mac_to_clks(ah, (uint32_t)eifs));
    REG_RMW(ah, AR_USEC,
            (ah->clockrate - 1) | SM(rx_lat, AR_USEC_RX_LAT) | SM(tx_lat, AR_USEC_TX_LAT),
            AR_USEC_TX_LAT | AR_USEC_RX_LAT | AR_USEC_USEC);
}

void ath9k_hw_get_delta_slope_vals(uint32_t coef_scaled, uint32_t *man, uint32_t *exp)
{
    uint32_t coef_exp, coef_man;

    for (coef_exp = 31; coef_exp > 0; coef_exp--)
        if ((coef_scaled >> coef_exp) & 0x1)
            break;

    coef_exp = 14 - (coef_exp - COEF_SCALE_S);
    coef_man = coef_scaled + (1U << (COEF_SCALE_S - coef_exp - 1));
    *man = coef_man >> (COEF_SCALE_S - coef_exp);
    *exp = coef_exp - 16;
}

static void set_delta_slope(struct ath_hw *ah, uint16_t freq)
{
    uint32_t coef_scaled, ds_coef_exp, ds_coef_man;

    coef_scaled = 0x64000000U / freq;
    ath9k_hw_get_delta_slope_vals(coef_scaled, &ds_coef_man, &ds_coef_exp);
    REG_RMW_FIELD(ah, AR_PHY_TIMING3, AR_PHY_TIMING3_DSC_MAN, ds_coef_man);
    REG_RMW_FIELD(ah, AR_PHY_TIMING3, AR_PHY_TIMING3_DSC_EXP, ds_coef_exp);

    coef_scaled = (9 * coef_scaled) / 10;
    ath9k_hw_get_delta_slope_vals(coef_scaled, &ds_coef_man, &ds_coef_exp);
    REG_RMW_FIELD(ah, AR_PHY_HALFGI, AR_PHY_HALFGI_DSC_MAN, ds_coef_man);
    REG_RMW_FIELD(ah, AR_PHY_HALFGI, AR_PHY_HALFGI_DSC_EXP, ds_coef_exp);
}

static void spur_mitigate(struct ath_hw *ah, uint16_t freq)
{
    uint16_t i;

    for (i = 0; i < ATH9K_EEP_MODAL_SPURS; i++) {
        uint16_t spur = ath9k_eeprom_spur_channel(ah, i);

        if (spur == AR_NO_SPUR)
            break;
        ath9k_log(ATH9K_LOG_WARN, "ath9k: EEPROM lists spur %u near %u MHz, spur mitigation not ported",
                  spur, freq);
        break;
    }
    REG_CLR_BIT(ah, AR_PHY_FORCE_CLKEN_CCK, AR_PHY_FORCE_CLKEN_CCK_MRC_MUX);
}

static int rf_set_freq(struct ath_hw *ah, uint16_t freq)
{
    uint32_t reg32 = REG_READ(ah, AR_PHY_SYNTH_CONTROL) & 0xc0000000;
    uint32_t channel_sel = CHANSEL_2G(freq);
    uint32_t txctl;

    txctl = REG_READ(ah, AR_PHY_CCK_TX_CTRL);
    if (freq == 2484)
        REG_WRITE(ah, AR_PHY_CCK_TX_CTRL, txctl | AR_PHY_CCK_TX_CTRL_JAPAN);
    else
        REG_WRITE(ah, AR_PHY_CCK_TX_CTRL, txctl & ~(uint32_t)AR_PHY_CCK_TX_CTRL_JAPAN);

    reg32 |= (1U << 29) | (1U << 28) | channel_sel;
    REG_WRITE(ah, AR_PHY_SYNTH_CONTROL, reg32);
    ah->cur_freq = freq;
    return 0;
}

static void override_ini(struct ath_hw *ah)
{
    uint32_t val;

    REG_SET_BIT(ah, AR_DIAG_SW, AR_DIAG_RX_DIS | AR_DIAG_RX_ABORT);
    val = REG_READ(ah, AR_PCU_MISC_MODE2) & ~(uint32_t)AR_ADHOC_MCAST_KEYID_ENABLE;
    val &= ~(uint32_t)AR_PCU_MISC_MODE2_HWWAR1;
    val |= AR_PCU_MISC_MODE2_CFP_IGNORE;
    REG_WRITE(ah, AR_PCU_MISC_MODE2, val);
}

static void set_channel_regs(struct ath_hw *ah)
{
    uint32_t enable_dac_fifo = REG_READ(ah, AR_PHY_TURBO) & AR_PHY_FC_ENABLE_DAC_FIFO;
    uint32_t phymode = AR_PHY_FC_HT_EN | AR_PHY_FC_SHORT_GI_40 | AR_PHY_FC_SINGLE_HT_LTF1 |
                       AR_PHY_FC_WALSH | enable_dac_fifo;

    REG_WRITE(ah, AR_PHY_TURBO, phymode);
    REG_WRITE(ah, AR_2040_MODE, 0);
    REG_WRITE(ah, AR_GTXTO, 25 << AR_GTXTO_TIMEOUT_LIMIT_S);
    REG_WRITE(ah, AR_CST, 0xF << AR_CST_TIMEOUT_LIMIT_S);
}

static void init_chain_masks(struct ath_hw *ah)
{
    REG_WRITE(ah, AR_PHY_RX_CHAINMASK, ah->rx_chainmask);
    REG_WRITE(ah, AR_PHY_CAL_CHAINMASK, ah->rx_chainmask);
    REG_WRITE(ah, AR_SELFGEN_MASK, ah->tx_chainmask);
}

static void apply_txpower(struct ath_hw *ah)
{
    ath9k_eeprom_set_txpower(ah, ah->cur_freq, ATH9K_SD_NO_CTL | ATH9K_CTL_11G, 0, 2 * 20);
}

static void write_ini_modes(struct ath_hw *ah, const uint32_t (*tbl)[5], size_t rows,
                            uint32_t col)
{
    size_t i;

    for (i = 0; i < rows; i++)
        REG_WRITE(ah, tbl[i][0], tbl[i][col]);
}

static int process_ini(struct ath_hw *ah)
{
    const uint32_t modes_index = 4;
    size_t i;

    REG_WRITE(ah, AR_PHY(0), 0x00000007);
    REG_WRITE(ah, AR_PHY_ADC_SERIAL_CTL, AR_PHY_SEL_EXTERNAL_RADIO);
    REG_WRITE(ah, AR_PHY_ADC_SERIAL_CTL, AR_PHY_SEL_INTERNAL_ADDAC);

    for (i = 0; i < AR9285MODES_9285_1_2_ROWS; i++) {
        uint32_t reg = ar9285Modes_9285_1_2[i][0];
        uint32_t val = ar9285Modes_9285_1_2[i][modes_index];

        REG_WRITE(ah, reg, val);
        if (reg >= 0x7800 && reg < 0x78a0)
            ath9k_udelay(100);
    }

    if (ath9k_eeprom_high_power(ah))
        write_ini_modes(ah, ar9285Modes_high_power_tx_gain_9285_1_2,
                        AR9285MODES_HIGH_POWER_TX_GAIN_9285_1_2_ROWS, modes_index);
    else
        write_ini_modes(ah, ar9285Modes_original_tx_gain_9285_1_2,
                        AR9285MODES_ORIGINAL_TX_GAIN_9285_1_2_ROWS, modes_index);

    for (i = 0; i < AR9285COMMON_9285_1_2_ROWS; i++) {
        uint32_t reg = ar9285Common_9285_1_2[i][0];

        REG_WRITE(ah, reg, ar9285Common_9285_1_2[i][1]);
        if (reg >= 0x7800 && reg < 0x78a0)
            ath9k_udelay(100);
    }

    override_ini(ah);
    set_channel_regs(ah);
    init_chain_masks(ah);
    apply_txpower(ah);
    return 0;
}

static void set_rfmode(struct ath_hw *ah)
{
    REG_WRITE(ah, AR_PHY_MODE, AR_PHY_MODE_DYNAMIC);
}

static void init_bb(struct ath_hw *ah)
{
    uint32_t synth_delay = REG_READ(ah, AR_PHY_RX_DELAY) & AR_PHY_RX_DELAY_DELAY;

    REG_WRITE(ah, AR_PHY_ACTIVE, AR_PHY_ACTIVE_EN);
    ath9k_udelay(synth_delay / 10 + BASE_ACTIVATE_DELAY);
}

static void init_qos(struct ath_hw *ah)
{
    REG_WRITE(ah, AR_MIC_QOS_CONTROL, 0x100aa);
    REG_WRITE(ah, AR_MIC_QOS_SELECT, 0x3210);
    REG_WRITE(ah, AR_QOS_NO_ACK,
              SM(2, AR_QOS_NO_ACK_TWO_BIT) | SM(5, AR_QOS_NO_ACK_BIT_OFF) |
              SM(0, AR_QOS_NO_ACK_BYTE_OFF));
    REG_WRITE(ah, AR_TXOP_X, AR_TXOP_X_VAL);
    REG_WRITE(ah, AR_TXOP_0_3, 0xFFFFFFFF);
    REG_WRITE(ah, AR_TXOP_4_7, 0xFFFFFFFF);
    REG_WRITE(ah, AR_TXOP_8_11, 0xFFFFFFFF);
    REG_WRITE(ah, AR_TXOP_12_15, 0xFFFFFFFF);
}

static void init_queues(struct ath_hw *ah)
{
    uint32_t q = ah->tx_queue;
    uint32_t cw_min;
    int i;

    for (i = 0; i < AR_NUM_DCU; i++)
        REG_WRITE(ah, AR_DQCUMASK(i), 1U << i);

    for (cw_min = 1; cw_min < 15; cw_min = (cw_min << 1) | 1)
        ;
    REG_WRITE(ah, AR_DLCL_IFS(q),
              SM(cw_min, AR_D_LCL_IFS_CWMIN) | SM(1023, AR_D_LCL_IFS_CWMAX) |
              SM(2, AR_D_LCL_IFS_AIFS));
    REG_WRITE(ah, AR_DRETRY_LIMIT(q),
              SM(32, AR_D_RETRY_LIMIT_STA_SH) | SM(32, AR_D_RETRY_LIMIT_STA_LG) |
              SM(10, AR_D_RETRY_LIMIT_FR_SH));
    REG_WRITE(ah, AR_QMISC(q), AR_Q_MISC_DCU_EARLY_TERM_REQ);
    REG_WRITE(ah, AR_DMISC(q), AR_D_MISC_CW_BKOFF_EN | AR_D_MISC_FRAG_WAIT_EN | 0x2);
    REG_WRITE(ah, AR_DCHNTIME(q), 0);
}

static void set_dma(struct ath_hw *ah)
{
    REG_SET_BIT(ah, AR_AHB_MODE, AR_AHB_PREFETCH_RD_EN);
    REG_RMW(ah, AR_TXCFG, AR_TXCFG_DMASZ_128B, AR_TXCFG_DMASZ_MASK);
    REG_RMW_FIELD(ah, AR_TXCFG, AR_FTRIG, ah->tx_trig_level);
    REG_RMW(ah, AR_RXCFG, AR_RXCFG_DMASZ_128B, AR_RXCFG_DMASZ_MASK);
    REG_WRITE(ah, AR_RXFIFO_CFG, 0x200);
    REG_WRITE(ah, AR_PCU_TXBUF_CTRL, AR_9285_PCU_TXBUF_CTRL_USABLE_SIZE);
}

static void reset_opmode(struct ath_hw *ah, uint32_t mac_sta_id1, uint32_t def_antenna)
{
    REG_RMW(ah, AR_STA_ID1,
            mac_sta_id1 | AR_STA_ID1_RTS_USE_DEF | ah->sta_id1_defaults,
            ~(uint32_t)AR_STA_ID1_SADH_MASK);
    REG_WRITE(ah, AR_BSSMSKL, 0xFFFFFFFF);
    REG_WRITE(ah, AR_BSSMSKU, 0xFFFF);
    REG_WRITE(ah, AR_DEF_ANTENNA, def_antenna);
    REG_WRITE(ah, AR_ISR, ~0U);
    REG_WRITE(ah, AR_RSSI_THR, INIT_RSSI_THR);
    REG_RMW(ah, AR_STA_ID1, AR_STA_ID1_KSRCH_MODE,
            AR_STA_ID1_STA_AP | AR_STA_ID1_ADHOC | AR_STA_ID1_KSRCH_MODE);
    REG_CLR_BIT(ah, AR_CFG, AR_CFG_AP_ADHOC_INDICATION);
}

static void init_interrupt_masks(struct ath_hw *ah)
{
    REG_WRITE(ah, AR_IER, 0);
    REG_WRITE(ah, AR_IMR, 0);
    REG_WRITE(ah, AR_IMR_S2, 0);
    REG_WRITE(ah, AR_INTR_SYNC_CAUSE(ah), 0xFFFFFFFF);
    REG_WRITE(ah, AR_INTR_SYNC_ENABLE(ah), 0);
    REG_WRITE(ah, AR_INTR_SYNC_MASK(ah), 0);
    REG_WRITE(ah, AR_INTR_ASYNC_ENABLE(ah), 0);
    REG_WRITE(ah, AR_INTR_ASYNC_MASK(ah), 0);
}

static void start_nfcal(struct ath_hw *ah, bool update)
{
    REG_SET_BIT(ah, AR_PHY_AGC_CONTROL(ah), AR_PHY_AGC_CONTROL_ENABLE_NF);
    if (update)
        REG_CLR_BIT(ah, AR_PHY_AGC_CONTROL(ah), AR_PHY_AGC_CONTROL_NO_UPDATE_NF);
    else
        REG_SET_BIT(ah, AR_PHY_AGC_CONTROL(ah), AR_PHY_AGC_CONTROL_NO_UPDATE_NF);
    REG_SET_BIT(ah, AR_PHY_AGC_CONTROL(ah), AR_PHY_AGC_CONTROL_NF);
}

static void load_nf(struct ath_hw *ah)
{
    uint32_t bb_agc_ctl = REG_READ(ah, AR_PHY_AGC_CONTROL(ah));
    int32_t nfval = ah->nf_nominal;
    uint32_t j;

    REG_RMW(ah, AR_PHY_CCA, (((uint32_t)nfval << 1) & 0x1ff), 0x1ff);

    if (bb_agc_ctl & AR_PHY_AGC_CONTROL_NF)
        REG_CLR_BIT(ah, AR_PHY_AGC_CONTROL(ah), AR_PHY_AGC_CONTROL_NF);

    REG_CLR_BIT(ah, AR_PHY_AGC_CONTROL(ah), AR_PHY_AGC_CONTROL_ENABLE_NF);
    REG_CLR_BIT(ah, AR_PHY_AGC_CONTROL(ah), AR_PHY_AGC_CONTROL_NO_UPDATE_NF);
    REG_SET_BIT(ah, AR_PHY_AGC_CONTROL(ah), AR_PHY_AGC_CONTROL_NF);

    for (j = 0; j < 22200; j++) {
        if ((REG_READ(ah, AR_PHY_AGC_CONTROL(ah)) & AR_PHY_AGC_CONTROL_NF) == 0)
            break;
        ath9k_udelay(10);
    }

    if (bb_agc_ctl & AR_PHY_AGC_CONTROL_NF) {
        if (bb_agc_ctl & AR_PHY_AGC_CONTROL_ENABLE_NF)
            REG_SET_BIT(ah, AR_PHY_AGC_CONTROL(ah), AR_PHY_AGC_CONTROL_ENABLE_NF);
        if (bb_agc_ctl & AR_PHY_AGC_CONTROL_NO_UPDATE_NF)
            REG_SET_BIT(ah, AR_PHY_AGC_CONTROL(ah), AR_PHY_AGC_CONTROL_NO_UPDATE_NF);
        REG_SET_BIT(ah, AR_PHY_AGC_CONTROL(ah), AR_PHY_AGC_CONTROL_NF);
    }
}

static bool cl_cal(struct ath_hw *ah)
{
    REG_SET_BIT(ah, AR_PHY_CL_CAL_CTL, AR_PHY_CL_CAL_ENABLE);
    REG_CLR_BIT(ah, AR_PHY_ADC_CTL, AR_PHY_ADC_CTL_OFF_PWDADC);
    REG_SET_BIT(ah, AR_PHY_AGC_CONTROL(ah), AR_PHY_AGC_CONTROL_FLTR_CAL);
    REG_SET_BIT(ah, AR_PHY_TPCRG1, AR_PHY_TPCRG1_PD_CAL_ENABLE);
    REG_SET_BIT(ah, AR_PHY_AGC_CONTROL(ah), AR_PHY_AGC_CONTROL_CAL);
    if (!ath9k_hw_wait(ah, AR_PHY_AGC_CONTROL(ah), AR_PHY_AGC_CONTROL_CAL, 0, ATH9K_WAIT_TIMEOUT)) {
        ath9k_log(ATH9K_LOG_WARN, "ath9k: offset calibration did not complete");
        return false;
    }
    REG_SET_BIT(ah, AR_PHY_ADC_CTL, AR_PHY_ADC_CTL_OFF_PWDADC);
    REG_CLR_BIT(ah, AR_PHY_CL_CAL_CTL, AR_PHY_CL_CAL_ENABLE);
    REG_CLR_BIT(ah, AR_PHY_AGC_CONTROL(ah), AR_PHY_AGC_CONTROL_FLTR_CAL);
    return true;
}

static bool clc(struct ath_hw *ah)
{
    uint32_t i, txgain_max, clc_gain, gain_mask = 0, clc_num = 0;
    uint32_t i0_num = 0, q0_num = 0, reg_rf2g5_org;
    bool ok = true;

    if (!cl_cal(ah))
        return false;

    txgain_max = MS(REG_READ(ah, AR_PHY_TX_PWRCTRL7), AR_PHY_TX_PWRCTRL_TX_GAIN_TAB_MAX);
    for (i = 0; i < txgain_max + 1; i++) {
        clc_gain = (REG_READ(ah, AR_PHY_TX_GAIN_TBL1 + (i << 2)) & AR_PHY_TX_GAIN_CLC) >>
                   AR_PHY_TX_GAIN_CLC_S;
        if (!(gain_mask & (1U << clc_gain))) {
            gain_mask |= 1U << clc_gain;
            clc_num++;
        }
    }
    for (i = 0; i < clc_num; i++) {
        uint32_t v = REG_READ(ah, AR_PHY_CLC_TBL1 + (i << 2));

        if (((v & AR_PHY_CLC_I0) >> AR_PHY_CLC_I0_S) == 0)
            i0_num++;
        if (((v & AR_PHY_CLC_Q0) >> AR_PHY_CLC_Q0_S) == 0)
            q0_num++;
    }
    if (i0_num + q0_num > AR9285_CLCAL_REDO_THRESH) {
        reg_rf2g5_org = REG_READ(ah, AR9285_RF2G5);
        REG_WRITE(ah, AR9285_RF2G5, (reg_rf2g5_org & AR9285_RF2G5_IC50TX) | AR9285_RF2G5_IC50TX_SET);
        ok = cl_cal(ah);
        REG_WRITE(ah, AR9285_RF2G5, reg_rf2g5_org);
    }
    return ok;
}

static void pa_cal(struct ath_hw *ah, bool is_reset)
{
    static const uint32_t reg_list_addr[] = {0x786c, 0x7854, 0x7820, 0x7824, 0x7868, 0x783c, 0x7838};
    uint32_t reg_list[7];
    uint32_t reg_val, ccomp_org, reg_field;
    int i, offset, offs_6_1, offs_0;

    if (ath9k_eeprom_high_power(ah))
        return;
    if (!is_reset && ah->pacal_skipcount) {
        ah->pacal_skipcount--;
        return;
    }

    for (i = 0; i < 7; i++)
        reg_list[i] = REG_READ(ah, reg_list_addr[i]);

    reg_val = REG_READ(ah, 0x7834);
    reg_val &= ~(uint32_t)0x1;
    REG_WRITE(ah, 0x7834, reg_val);
    reg_val = REG_READ(ah, 0x9808);
    reg_val |= (0x1U << 27);
    REG_WRITE(ah, 0x9808, reg_val);

    REG_RMW_FIELD(ah, AR9285_AN_TOP3, AR9285_AN_TOP3_PWDDAC, 1);
    REG_RMW_FIELD(ah, AR9285_AN_RXTXBB1, AR9285_AN_RXTXBB1_PDRXTXBB1, 1);
    REG_RMW_FIELD(ah, AR9285_AN_RXTXBB1, AR9285_AN_RXTXBB1_PDV2I, 1);
    REG_RMW_FIELD(ah, AR9285_AN_RXTXBB1, AR9285_AN_RXTXBB1_PDDACIF, 1);
    REG_RMW_FIELD(ah, AR9285_AN_RF2G2, AR9285_AN_RF2G2_OFFCAL, 0);
    REG_RMW_FIELD(ah, AR9285_AN_RF2G7, AR9285_AN_RF2G7_PWDDB, 0);
    REG_RMW_FIELD(ah, AR9285_AN_RF2G1, AR9285_AN_RF2G1_ENPACAL, 0);
    REG_RMW_FIELD(ah, AR9285_AN_RF2G1, AR9285_AN_RF2G1_PDPADRV1, 0);
    REG_RMW_FIELD(ah, AR9285_AN_RF2G1, AR9285_AN_RF2G1_PDPADRV2, 0);
    REG_RMW_FIELD(ah, AR9285_AN_RF2G1, AR9285_AN_RF2G1_PDPAOUT, 0);
    REG_RMW_FIELD(ah, AR9285_AN_RF2G8, AR9285_AN_RF2G8_PADRVGN2TAB0, 7);
    REG_RMW_FIELD(ah, AR9285_AN_RF2G7, AR9285_AN_RF2G7_PADRVGN2TAB0, 0);
    ccomp_org = MS(REG_READ(ah, AR9285_AN_RF2G6), AR9285_AN_RF2G6_CCOMP);
    REG_RMW_FIELD(ah, AR9285_AN_RF2G6, AR9285_AN_RF2G6_CCOMP, 0xf);

    REG_WRITE(ah, AR9285_AN_TOP2, 0xca0358a0);
    ath9k_udelay(30);
    REG_RMW_FIELD(ah, AR9285_AN_RF2G6, AR9285_AN_RF2G6_OFFS, 0);
    REG_RMW_FIELD(ah, AR9285_AN_RF2G3, AR9285_AN_RF2G3_PDVCCOMP, 0);

    for (i = 6; i > 0; i--) {
        reg_val = REG_READ(ah, 0x7834);
        reg_val |= (1U << (19 + i));
        REG_WRITE(ah, 0x7834, reg_val);
        ath9k_udelay(1);
        reg_val = REG_READ(ah, 0x7834);
        reg_val &= ~(0x1U << (19 + i));
        reg_field = MS(REG_READ(ah, 0x7840), AR9285_AN_RXTXBB1_SPARE9);
        reg_val |= (reg_field << (19 + i));
        REG_WRITE(ah, 0x7834, reg_val);
    }

    REG_RMW_FIELD(ah, AR9285_AN_RF2G3, AR9285_AN_RF2G3_PDVCCOMP, 1);
    ath9k_udelay(1);
    reg_field = MS(REG_READ(ah, AR9285_AN_RF2G9), AR9285_AN_RXTXBB1_SPARE9);
    REG_RMW_FIELD(ah, AR9285_AN_RF2G3, AR9285_AN_RF2G3_PDVCCOMP, reg_field);
    offs_6_1 = (int)MS(REG_READ(ah, AR9285_AN_RF2G6), AR9285_AN_RF2G6_OFFS);
    offs_0 = (int)MS(REG_READ(ah, AR9285_AN_RF2G3), AR9285_AN_RF2G3_PDVCCOMP);

    offset = (offs_6_1 << 1) | offs_0;
    offs_6_1 = offset >> 1;
    offs_0 = offset & 1;

    if (!is_reset && ah->pacal_prev_offset == offset) {
        if (ah->pacal_max_skipcount < 5)
            ah->pacal_max_skipcount = (uint8_t)(2 * ah->pacal_max_skipcount);
        ah->pacal_skipcount = ah->pacal_max_skipcount;
    } else {
        ah->pacal_max_skipcount = 1;
        ah->pacal_skipcount = 0;
        ah->pacal_prev_offset = offset;
    }

    REG_RMW_FIELD(ah, AR9285_AN_RF2G6, AR9285_AN_RF2G6_OFFS, offs_6_1);
    REG_RMW_FIELD(ah, AR9285_AN_RF2G3, AR9285_AN_RF2G3_PDVCCOMP, offs_0);

    reg_val = REG_READ(ah, 0x7834);
    reg_val |= 0x1;
    REG_WRITE(ah, 0x7834, reg_val);
    reg_val = REG_READ(ah, 0x9808);
    reg_val &= ~(0x1U << 27);
    REG_WRITE(ah, 0x9808, reg_val);

    for (i = 0; i < 7; i++)
        REG_WRITE(ah, reg_list_addr[i], reg_list[i]);
    REG_RMW_FIELD(ah, AR9285_AN_RF2G6, AR9285_AN_RF2G6_CCOMP, ccomp_org);
}

static bool init_cal(struct ath_hw *ah)
{
    if (!clc(ah))
        return false;
    pa_cal(ah, true);
    load_nf(ah);
    start_nfcal(ah, true);
    return true;
}

static bool chip_reset(struct ath_hw *ah)
{
    int reset_type = ATH9K_RESET_WARM;

    if (ah->chip_fullsleep || REG_READ(ah, AR_Q_TXE) || (REG_READ(ah, AR_CR) & AR_CR_RXE(ah)))
        reset_type = ATH9K_RESET_COLD;

    if (!set_reset_reg(ah, reset_type))
        return false;
    if (!set_power(ah, ATH9K_PM_AWAKE))
        return false;
    ah->chip_fullsleep = false;
    init_pll(ah);
    return true;
}

int ath9k_hw_reset(struct ath_hw *ah, uint16_t freq)
{
    uint32_t save_def_antenna, mac_sta_id1, save_led;

    if (!ah->initialized || freq < 2412 || freq > 2484)
        return -1;
    if (!set_power(ah, ATH9K_PM_AWAKE))
        return -1;

    save_def_antenna = REG_READ(ah, AR_DEF_ANTENNA);
    if (save_def_antenna == 0)
        save_def_antenna = 1;
    mac_sta_id1 = REG_READ(ah, AR_STA_ID1) & AR_STA_ID1_BASE_RATE_11B;
    save_led = REG_READ(ah, AR_CFG_LED) &
               (AR_CFG_LED_ASSOC_CTL | AR_CFG_LED_MODE_SEL | AR_CFG_LED_BLINK_THRESH_SEL |
                AR_CFG_LED_BLINK_SLOW);

    REG_WRITE(ah, AR_PHY_ACTIVE, AR_PHY_ACTIVE_DIS);

    if (!chip_reset(ah)) {
        ath9k_log(ATH9K_LOG_ERR, "ath9k: chip reset failed");
        return -1;
    }

    ah->cur_freq = freq;
    REG_SET_BIT(ah, AR_GPIO_INPUT_EN_VAL(ah), AR_GPIO_JTAG_DISABLE);

    if (process_ini(ah) != 0)
        return -1;

    set_rfmode(ah);
    REG_RMW_FIELD(ah, AR_AES_MUTE_MASK1, AR_AES_MUTE_MASK1_FC_MGMT, 0xc7ff);
    set_delta_slope(ah, freq);
    spur_mitigate(ah, freq);
    ath9k_eeprom_set_board_values(ah);
    reset_opmode(ah, mac_sta_id1, save_def_antenna);

    if (rf_set_freq(ah, freq) != 0)
        return -1;

    set_clockrate(ah);
    init_queues(ah);
    init_interrupt_masks(ah);
    init_qos(ah);
    init_global_settings(ah);
    REG_SET_BIT(ah, AR_STA_ID1, AR_STA_ID1_PRESERVE_SEQNUM);
    set_dma(ah);
    REG_WRITE(ah, AR_OBS(ah), 8);
    init_bb(ah);

    if (!init_cal(ah))
        return -1;

    REG_WRITE(ah, AR_PHY_RX_CHAINMASK, ah->rx_chainmask);
    REG_WRITE(ah, AR_PHY_CAL_CHAINMASK, ah->rx_chainmask);
    REG_WRITE(ah, AR_CFG_LED, save_led | AR_CFG_SCLK_32KHZ);
    return 0;
}

bool ath9k_hw_disable(struct ath_hw *ah)
{
    if (!set_power(ah, ATH9K_PM_AWAKE))
        return false;
    if (!set_reset_reg(ah, ATH9K_RESET_COLD))
        return false;
    init_pll(ah);
    return true;
}

void ath9k_hw_set_macaddr(struct ath_hw *ah, const uint8_t mac[6])
{
    uint32_t lo = (uint32_t)mac[0] | ((uint32_t)mac[1] << 8) | ((uint32_t)mac[2] << 16) |
                  ((uint32_t)mac[3] << 24);
    uint32_t hi = REG_READ(ah, AR_STA_ID1) & ~(uint32_t)AR_STA_ID1_SADH_MASK;

    memcpy(ah->mac_addr, mac, 6);
    REG_WRITE(ah, AR_STA_ID0, lo);
    REG_WRITE(ah, AR_STA_ID1, hi | (uint32_t)mac[4] | ((uint32_t)mac[5] << 8));
}

void ath9k_hw_set_bssid(struct ath_hw *ah, const uint8_t bssid[6], uint16_t aid)
{
    REG_WRITE(ah, AR_BSS_ID0, (uint32_t)bssid[0] | ((uint32_t)bssid[1] << 8) |
                                  ((uint32_t)bssid[2] << 16) | ((uint32_t)bssid[3] << 24));
    REG_WRITE(ah, AR_BSS_ID1, (uint32_t)bssid[4] | ((uint32_t)bssid[5] << 8) |
                                  (((uint32_t)aid & 0x3fff) << AR_BSS_ID1_AID_S));
}

void ath9k_hw_set_rx_filter(struct ath_hw *ah, uint32_t bits)
{
    REG_WRITE(ah, AR_RX_FILTER, bits);
    REG_WRITE(ah, AR_PHY_ERR, 0);
    REG_CLR_BIT(ah, AR_RXCFG, AR_RXCFG_ZLFDMA);
}

struct ds_layout {
    uint32_t link;
    uint32_t data;
    uint32_t ctl0;
    uint32_t ctl1;
    uint32_t w[20];
};

#define DS(p) ((volatile struct ds_layout *)(p))
#define DS_CTL(i) w[(i) - 2]
#define DS_TXSTATUS(i) w[10 + (i)]
#define DS_RXSTATUS(i) w[(i)]

static void rx_buf_link(struct ath_hw *ah, int idx)
{
    volatile struct ds_layout *ds = DS(ah->rx[idx].desc);

    ds->link = 0;
    ds->data = ah->rx[idx].data_phys;
    ds->ctl1 = ATH9K_RX_BUF_SIZE & 0xfff;
    for (int i = 0; i < 9; i++)
        ds->DS_RXSTATUS(i) = 0;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);

    if (ah->rx_last >= 0)
        DS(ah->rx[ah->rx_last].desc)->link = ah->rx[idx].desc_phys;
    else
        REG_WRITE(ah, AR_RXDP, ah->rx[idx].desc_phys);
    ah->rx_last = idx;
}

static void rx_fifo_push(struct ath_hw *ah, int idx)
{
    ah->rx_fifo[(ah->rx_head + ah->rx_count) % ATH9K_RX_RING] = (uint8_t)idx;
    ah->rx_count++;
}

void ath9k_hw_rx_release(struct ath_hw *ah)
{
    if (ah->rx_ring_phys) {
        for (int i = 0; i < ATH9K_RX_RING; i++) {
            if (ah->rx[i].data_phys)
                ath9k_dma_free_page(ah->rx[i].data_phys);
            ah->rx[i].data_phys = 0;
            ah->rx[i].data = NULL;
        }
        ath9k_dma_free_page(ah->rx_ring_phys);
        ah->rx_ring_phys = 0;
        ah->rx_ring = NULL;
    }
    ah->rx_running = false;
}

int ath9k_hw_rx_start(struct ath_hw *ah)
{
    void *virt = NULL;

    if (!ah->rx_ring_phys) {
        ah->rx_ring_phys = ath9k_dma_alloc_page(&virt);
        if (!ah->rx_ring_phys)
            return -1;
        ah->rx_ring = virt;
        memset((void *)ah->rx_ring, 0, 4096);
        for (int i = 0; i < ATH9K_RX_RING; i++) {
            uint64_t bp = ath9k_dma_alloc_page(&virt);

            if (!bp) {
                ath9k_hw_rx_release(ah);
                return -1;
            }
            memset(virt, 0, 4096);
            ah->rx[i].desc = ah->rx_ring + (size_t)i * AR_DESC_STRIDE;
            ah->rx[i].desc_phys = (uint32_t)(ah->rx_ring_phys + (uint64_t)i * AR_DESC_STRIDE);
            ah->rx[i].data = virt;
            ah->rx[i].data_phys = (uint32_t)bp;
        }
    }

    ah->rx_head = 0;
    ah->rx_count = 0;
    ah->rx_hold = -1;
    ah->rx_last = -1;
    for (int i = 0; i < ATH9K_RX_RING; i++) {
        rx_fifo_push(ah, i);
        rx_buf_link(ah, i);
    }
    ah->rx_hold = -1;

    REG_WRITE(ah, AR_CR, AR_CR_RXE(ah));
    REG_CLR_BIT(ah, AR_DIAG_SW, AR_DIAG_RX_DIS | AR_DIAG_RX_ABORT);
    ah->rx_running = true;
    return 0;
}

void ath9k_hw_rx_stop(struct ath_hw *ah)
{
    uint32_t i;

    REG_SET_BIT(ah, AR_DIAG_SW, AR_DIAG_RX_ABORT | AR_DIAG_RX_DIS);
    REG_WRITE(ah, AR_CR, AR_CR_RXD);
    for (i = 0; i < 10000 / ATH9K_TIME_QUANTUM; i++) {
        if ((REG_READ(ah, AR_CR) & AR_CR_RXE(ah)) == 0)
            break;
        ath9k_udelay(ATH9K_TIME_QUANTUM);
    }
    ah->rx_running = false;
}

static bool rx_desc_done(volatile struct ds_layout *ds)
{
    return (ds->DS_RXSTATUS(8) & AR_RxDone) != 0;
}

bool ath9k_hw_rx_next(struct ath_hw *ah, struct ath9k_rx_frame *frame)
{
    while (ah->rx_running && ah->rx_count) {
        int idx = ah->rx_fifo[ah->rx_head];
        volatile struct ds_layout *ds = DS(ah->rx[idx].desc);
        uint32_t s1, s4, s8, s0;
        uint16_t len;

        if (idx == ah->rx_hold)
            return false;

        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if (!rx_desc_done(ds)) {
            if (ah->rx_count < 2)
                return false;
            if (!rx_desc_done(DS(ah->rx[ah->rx_fifo[(ah->rx_head + 1) % ATH9K_RX_RING]].desc)))
                return false;
            if (rx_desc_done(ds) == false) {
                ah->rx_head = (uint8_t)((ah->rx_head + 1) % ATH9K_RX_RING);
                ah->rx_count--;
                rx_fifo_push(ah, idx);
                if (ah->rx_hold >= 0)
                    rx_buf_link(ah, ah->rx_hold);
                ah->rx_hold = idx;
                continue;
            }
        }

        s0 = ds->DS_RXSTATUS(0);
        s1 = ds->DS_RXSTATUS(1);
        s4 = ds->DS_RXSTATUS(4);
        s8 = ds->DS_RXSTATUS(8);
        len = (uint16_t)(s1 & AR_DataLen);

        frame->data = ah->rx[idx].data;
        frame->len = len;
        frame->rssi = (int8_t)MS(s4, AR_RxRSSICombined);
        frame->rate = (uint8_t)MS(s0, AR_RxRate);
        frame->status = (uint8_t)(((s8 & AR_RxFrameOK) ? 0 : 1) | ((s1 & AR_RxMore) ? 2 : 0));

        ah->rx_head = (uint8_t)((ah->rx_head + 1) % ATH9K_RX_RING);
        ah->rx_count--;
        rx_fifo_push(ah, idx);
        if (ah->rx_hold >= 0)
            rx_buf_link(ah, ah->rx_hold);
        ah->rx_hold = idx;

        if (frame->status || len < 10 || len > ATH9K_RX_BUF_SIZE)
            continue;
        return true;
    }
    return false;
}

void ath9k_hw_tx_release(struct ath_hw *ah)
{
    if (ah->tx_ring_phys) {
        for (int i = 0; i < ATH9K_TX_RING; i++) {
            if (ah->tx[i].buf_phys)
                ath9k_dma_free_page(ah->tx[i].buf_phys);
            ah->tx[i].buf_phys = 0;
            ah->tx[i].buf = NULL;
        }
        ath9k_dma_free_page(ah->tx_ring_phys);
        ah->tx_ring_phys = 0;
        ah->tx_ring = NULL;
    }
}

int ath9k_hw_tx_init(struct ath_hw *ah)
{
    void *virt = NULL;

    ah->tx_queue = 0;
    ah->tx_ring_phys = ath9k_dma_alloc_page(&virt);
    if (!ah->tx_ring_phys)
        return -1;
    ah->tx_ring = virt;
    memset((void *)ah->tx_ring, 0, 4096);
    for (int i = 0; i < ATH9K_TX_RING; i++) {
        uint64_t bp = ath9k_dma_alloc_page(&virt);

        if (!bp) {
            ath9k_hw_tx_release(ah);
            return -1;
        }
        memset(virt, 0, 4096);
        ah->tx[i].buf = virt;
        ah->tx[i].buf_phys = (uint32_t)bp;
        ah->tx[i].desc_phys = (uint32_t)(ah->tx_ring_phys + (uint64_t)i * AR_DESC_STRIDE);
        ah->tx[i].pending = false;
    }
    ah->tx_next = 0;
    return 0;
}

void ath9k_hw_tx_reap(struct ath_hw *ah)
{
    for (int i = 0; i < ATH9K_TX_RING; i++) {
        volatile struct ds_layout *ds;
        uint32_t st9, st1;

        if (!ah->tx[i].pending)
            continue;
        ds = DS(ah->tx_ring + (size_t)i * AR_DESC_STRIDE);
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        st9 = ds->DS_TXSTATUS(9);
        if (!(st9 & AR_TxDone))
            continue;
        st1 = ds->DS_TXSTATUS(1);
        if (st1 & AR_FrmXmitOK)
            ah->tx_ok++;
        else
            ah->tx_fail++;
        ah->tx[i].pending = false;
    }
}

bool ath9k_hw_tx_frame(struct ath_hw *ah, const uint8_t *frame, uint16_t len,
                       uint8_t hw_rate, bool no_ack)
{
    volatile struct ds_layout *ds;
    uint8_t idx;
    uint32_t ctl1, ctl2, ctl3, ctl7;
    uint32_t pkt_len = (uint32_t)len + 4;

    if (!ah->tx_ring || !frame || !len || len > ATH9K_MAX_TX_LEN)
        return false;

    ath9k_hw_tx_reap(ah);
    idx = ah->tx_next;
    if (ah->tx[idx].pending)
        return false;

    memcpy(ah->tx[idx].buf, frame, len);
    ah->tx[idx].len = len;

    ds = DS(ah->tx_ring + (size_t)idx * AR_DESC_STRIDE);
    for (int i = 0; i < 20; i++)
        ds->w[i] = 0;
    ds->link = 0;
    ds->data = ah->tx[idx].buf_phys;

    ctl1 = (uint32_t)len | SM(0, AR_FrameType) | (no_ack ? AR_NoAck : 0);
    ctl2 = SM(no_ack ? 1 : 4, AR_XmitDataTries0);
    ctl3 = SM(hw_rate, AR_XmitRate0);
    ctl7 = SM(ah->tx_chainmask, AR_ChainSel0);

    ds->ctl0 = (pkt_len & AR_FrameLen) | SM(ATH9K_MAX_RATE_POWER, AR_XmitPower0) | AR_ClrDestMask;
    ds->ctl1 = ctl1;
    ds->DS_CTL(2) = ctl2;
    ds->DS_CTL(3) = ctl3;
    ds->DS_CTL(4) = 0;
    ds->DS_CTL(5) = 0;
    ds->DS_CTL(6) = 0;
    ds->DS_CTL(7) = ctl7;
    ds->DS_CTL(8) = 0;
    ds->DS_CTL(9) = SM(ATH9K_MAX_RATE_POWER, AR_XmitPower1);
    ds->DS_CTL(10) = SM(ATH9K_MAX_RATE_POWER, AR_XmitPower2);
    ds->DS_CTL(11) = SM(ATH9K_MAX_RATE_POWER, AR_XmitPower3);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);

    ah->tx[idx].pending = true;
    ah->tx_next = (uint8_t)((idx + 1) % ATH9K_TX_RING);

    REG_WRITE(ah, AR_QTXDP(ah->tx_queue), ah->tx[idx].desc_phys);
    REG_WRITE(ah, AR_Q_TXE, 1U << ah->tx_queue);
    return true;
}
