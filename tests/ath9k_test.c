#include "ath9k_priv.h"
#include "ath9k_initvals.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #expr); \
        exit(1); \
    } \
} while (0)

extern void (*ath9k_test_read_hook)(struct ath_hw *ah, uint32_t reg);
extern void (*ath9k_test_write_hook)(struct ath_hw *ah, uint32_t reg, uint32_t val);

#define REG_SPACE 0x20000
#define SREV_AR9285_12 0x000C02FFU

static uint8_t *regs_mem;
static uint16_t eeprom_words[64 + sizeof(struct ath9k_eeprom_4k) / 2];
static void *dma_pages[256];
static unsigned dma_count;
static uint32_t last_txdp, last_txe;

void ath9k_udelay(uint32_t us) { (void)us; }

void ath9k_log(int level, const char *fmt, ...)
{
    (void)level;
    (void)fmt;
}

uint64_t ath9k_dma_alloc_page(void **virt)
{
    void *p = NULL;

    if (dma_count >= 256 || posix_memalign(&p, 4096, 4096) != 0)
        return 0;
    dma_pages[dma_count] = p;
    *virt = p;
    return 0x10000000ULL + (uint64_t)(dma_count++) * 4096;
}

void ath9k_dma_free_page(uint64_t phys) { (void)phys; }

static uint32_t *reg_ptr(uint32_t reg) { return (uint32_t *)(regs_mem + reg); }

static void read_hook(struct ath_hw *ah, uint32_t reg)
{
    (void)ah;
    if (reg >= ATH9K_EEPROM_OFFSET && reg < ATH9K_EEPROM_OFFSET + 0x4000) {
        uint32_t word = (reg - ATH9K_EEPROM_OFFSET) >> 2;

        *reg_ptr(AR_EEPROM_STATUS_DATA(0)) = word < sizeof(eeprom_words) / 2 ? eeprom_words[word] : 0;
    }
}

static void write_hook(struct ath_hw *ah, uint32_t reg, uint32_t val)
{
    (void)ah;
    if (reg == AR_PHY_AGC_CONTROL(0))
        *reg_ptr(reg) = val & ~(uint32_t)(AR_PHY_AGC_CONTROL_CAL | AR_PHY_AGC_CONTROL_NF);
    if (reg == AR_QTXDP(0))
        last_txdp = val;
    if (reg == AR_Q_TXE)
        last_txe = val;
}

static void build_eeprom(void)
{
    struct ath9k_eeprom_4k eep;
    uint16_t *w = (uint16_t *)&eep;
    uint16_t sum = 0;
    size_t i, n = sizeof(eep) / 2;

    memset(&eep, 0, sizeof(eep));
    eep.base.length = (uint16_t)sizeof(eep);
    eep.base.version = (uint16_t)((ATH9K_EEP_VER << 12) | 0x0004);
    eep.base.tx_mask = 1;
    eep.base.rx_mask = 1;
    eep.base.mac_addr[0] = 0x00;
    eep.base.mac_addr[1] = 0x1D;
    eep.base.mac_addr[2] = 0x0F;
    eep.base.mac_addr[3] = 0x12;
    eep.base.mac_addr[4] = 0x34;
    eep.base.mac_addr[5] = 0x56;
    eep.modal.version = 3;
    eep.modal.xpd_gain = 0x3;
    eep.modal.pd_gain_overlap = 2;

    eep.cal_freq_pier_2g[0] = 112;
    eep.cal_freq_pier_2g[1] = 137;
    eep.cal_freq_pier_2g[2] = 162;
    for (int p = 0; p < ATH9K_EEP4K_NUM_2G_CAL_PIERS; p++) {
        for (int g = 0; g < ATH9K_EEP4K_NUM_PD_GAINS; g++) {
            for (int k = 0; k < ATH9K_PD_GAIN_ICEPTS; k++) {
                eep.cal_pier_data_2g[0][p].pwr_pdg[g][k] = (uint8_t)(g * 8 + k * 6 + p);
                eep.cal_pier_data_2g[0][p].vpd_pdg[g][k] = (uint8_t)(20 + g * 4 + k * 12 + p);
            }
        }
    }
    for (int p = 0; p < 3; p++) {
        eep.cal_target_power_cck[p].b_channel = (uint8_t)(p == 0 ? 112 : p == 1 ? 137 : 255);
        eep.cal_target_power_2g[p].b_channel = (uint8_t)(p == 0 ? 112 : p == 1 ? 137 : 255);
        eep.cal_target_power_2g_ht20[p].b_channel = (uint8_t)(p == 0 ? 112 : p == 1 ? 137 : 255);
        for (int r = 0; r < 4; r++) {
            eep.cal_target_power_cck[p].t_pow2x[r] = (uint8_t)(34 - r);
            eep.cal_target_power_2g[p].t_pow2x[r] = (uint8_t)(30 - r * 2);
        }
        for (int r = 0; r < 8; r++)
            eep.cal_target_power_2g_ht20[p].t_pow2x[r] = (uint8_t)(28 - r);
    }

    for (i = 0; i < n; i++)
        sum ^= w[i];
    eep.base.checksum = (uint16_t)(eep.base.checksum ^ sum ^ 0xffff);

    memset(eeprom_words, 0, sizeof(eeprom_words));
    eeprom_words[0] = ATH9K_EEPROM_MAGIC;
    memcpy(&eeprom_words[64], &eep, sizeof(eep));
}

static void setup(uint32_t srev)
{
    memset(regs_mem, 0, REG_SPACE);
    *reg_ptr(AR_SREV(0)) = srev;
    *reg_ptr(AR_RTC_STATUS(0)) = AR_RTC_STATUS_ON;
    ath9k_test_read_hook = read_hook;
    ath9k_test_write_hook = write_hook;
    build_eeprom();
}

static void test_delta_slope(void)
{
    uint32_t man = 0, exp = 0;

    ath9k_hw_get_delta_slope_vals(1U << 24, &man, &exp);
    CHECK(man == 16384);
    CHECK(exp == 0xFFFFFFFEU);
}

static void test_interpolation(void)
{
    uint8_t pwr[5] = {10, 20, 30, 40, 50};
    uint8_t vpd[5] = {0, 10, 20, 30, 40};
    uint8_t out[32];
    uint16_t l = 0, r = 0;

    CHECK(ath9k_hw_interpolate(15, 10, 20, 100, 200) == 150);
    CHECK(ath9k_hw_interpolate(10, 10, 10, 77, 99) == 77);
    CHECK(ath9k_hw_get_lower_upper_index(5, pwr, 5, &l, &r) && l == 0 && r == 0);
    CHECK(ath9k_hw_get_lower_upper_index(60, pwr, 5, &l, &r) && l == 4 && r == 4);
    CHECK(ath9k_hw_get_lower_upper_index(30, pwr, 5, &l, &r) && l == 2 && r == 2);
    CHECK(!ath9k_hw_get_lower_upper_index(25, pwr, 5, &l, &r) && l == 1 && r == 2);
    memset(out, 0, sizeof(out));
    ath9k_hw_fill_vpd_table(10, 50, pwr, vpd, 5, out);
    CHECK(out[0] == 0 && out[1] == 5 && out[2] == 10 && out[20] == 40);
}

static void test_srev_decoding(struct ath_hw *ah)
{
    setup(0x000000C0U);
    CHECK(ath9k_hw_init(ah, regs_mem) != 0);
    CHECK(ah->mac_version == AR_SREV_VERSION_5416_PCIE);

    setup(0xFFFFFFFFU);
    CHECK(ath9k_hw_init(ah, regs_mem) != 0);

    setup(SREV_AR9285_12);
    CHECK(ath9k_hw_init(ah, regs_mem) == 0);
    CHECK(ah->mac_version == AR_SREV_VERSION_9285);
    CHECK(ah->mac_rev == 2);
    CHECK(ah->initialized);
}

static void test_eeprom_and_mac(struct ath_hw *ah)
{
    CHECK(ath9k_eeprom_ver(ah) == ATH9K_EEP_VER);
    CHECK(ath9k_eeprom_rev(ah) == 4);
    CHECK(!ath9k_eeprom_high_power(ah));
    CHECK(ah->mac_addr[0] == 0x00 && ah->mac_addr[1] == 0x1D && ah->mac_addr[5] == 0x56);

    eeprom_words[64 + 30] ^= 0x0101;
    CHECK(ath9k_eeprom_init(ah) != 0);
    eeprom_words[64 + 30] ^= 0x0101;
    CHECK(ath9k_eeprom_init(ah) == 0);

    eeprom_words[0] = 0x1234;
    CHECK(ath9k_eeprom_init(ah) != 0);
    eeprom_words[0] = ATH9K_EEPROM_MAGIC;
}

static void test_reset(struct ath_hw *ah)
{
    uint32_t prev = 0;
    uint32_t synth;

    CHECK(ath9k_hw_reset(ah, 2437) == 0);
    CHECK(ah->cur_freq == 2437);
    CHECK(*reg_ptr(AR_PHY_ACTIVE) == AR_PHY_ACTIVE_EN);
    CHECK(*reg_ptr(AR_PHY_RX_CHAINMASK) == 1);
    CHECK(*reg_ptr(AR_SELFGEN_MASK) == 1);

    synth = *reg_ptr(AR_PHY_SYNTH_CONTROL);
    CHECK((synth & (1U << 29)) && (synth & (1U << 28)));
    CHECK((synth & 0x3ffffff) == CHANSEL_2G(2437));

    CHECK(*reg_ptr(AR_PHY_POWER_TX_RATE1) != 0);
    CHECK((*reg_ptr(AR_PHY_POWER_TX_RATE1) & 0x3f) == 40);
    CHECK((*reg_ptr(AR_PHY_POWER_TX_RATE2) & 0x3f) == 40 - 2 * 3 + 0 - 0 + 0 + 0 * 0 + 0);
    CHECK(*reg_ptr(AR_PHY_POWER_TX_RATE_MAX) == ATH9K_MAX_RATE_POWER);
    CHECK(MS(*reg_ptr(AR_PHY_TPCRG1), AR_PHY_TPCRG1_NUM_PD_GAIN) == 1);

    for (int j = 0; j < 32; j++) {
        uint32_t v = *reg_ptr(AR_PHY_BASE + ((672 + j) << 2));

        for (int b = 0; b < 4; b++) {
            uint32_t byte = (v >> (8 * b)) & 0xff;

            CHECK(byte >= prev);
            prev = byte;
        }
    }
    CHECK(*reg_ptr(AR_RTC_PLL_CONTROL(0)) == (SM(5, AR_RTC_9160_PLL_REFDIV) | SM(0x2c, AR_RTC_9160_PLL_DIV)));

    CHECK(ath9k_hw_reset(ah, 2484) == 0);
    CHECK(*reg_ptr(AR_PHY_CCK_TX_CTRL) & AR_PHY_CCK_TX_CTRL_JAPAN);
    CHECK(ath9k_hw_reset(ah, 2412) == 0);
    CHECK(!(*reg_ptr(AR_PHY_CCK_TX_CTRL) & AR_PHY_CCK_TX_CTRL_JAPAN));
    CHECK(ath9k_hw_reset(ah, 2400) != 0);
    CHECK(ath9k_channel_to_freq(1) == 2412 && ath9k_channel_to_freq(6) == 2437);
    CHECK(ath9k_channel_to_freq(14) == 2484 && ath9k_channel_to_freq(0) == 0);
}

static void test_mac_registers(struct ath_hw *ah)
{
    static const uint8_t bssid[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};

    ath9k_hw_set_macaddr(ah, ah->mac_addr);
    CHECK(*reg_ptr(AR_STA_ID0) == 0x120F1D00U);
    CHECK((*reg_ptr(AR_STA_ID1) & AR_STA_ID1_SADH_MASK) == 0x5634);

    ath9k_hw_set_bssid(ah, bssid, 7);
    CHECK(*reg_ptr(AR_BSS_ID0) == 0x33221100U);
    CHECK((*reg_ptr(AR_BSS_ID1) & 0xffff) == 0x5544);
    CHECK(((*reg_ptr(AR_BSS_ID1) & AR_BSS_ID1_AID) >> AR_BSS_ID1_AID_S) == 7);

    ath9k_hw_set_rx_filter(ah, ATH9K_RX_FILTER_UCAST | ATH9K_RX_FILTER_BEACON);
    CHECK(*reg_ptr(AR_RX_FILTER) == 0x11);
}

static volatile uint32_t *rx_status(struct ath_hw *ah, int idx)
{
    return (volatile uint32_t *)(ah->rx[idx].desc) + 4;
}

static void complete_rx(struct ath_hw *ah, int idx, uint16_t len, uint8_t rssi, bool ok)
{
    volatile uint32_t *st = rx_status(ah, idx);

    memset(ah->rx[idx].data, 0, 64);
    ah->rx[idx].data[0] = 0x80;
    st[0] = (uint32_t)AR_RxRate & ((uint32_t)0x0b << AR_RxRate_S);
    st[1] = len;
    st[4] = (uint32_t)rssi << AR_RxRSSICombined_S;
    st[8] = AR_RxDone | (ok ? AR_RxFrameOK : 0);
}

static void test_rx_ring(struct ath_hw *ah)
{
    struct ath9k_rx_frame fr;
    volatile uint32_t *d0;

    CHECK(ath9k_hw_rx_start(ah) == 0);
    CHECK(*reg_ptr(AR_RXDP) == ah->rx[0].desc_phys);
    CHECK(*reg_ptr(AR_CR) == AR_CR_RXE(0));
    d0 = (volatile uint32_t *)ah->rx[0].desc;
    CHECK(d0[0] == ah->rx[1].desc_phys);
    CHECK(d0[1] == ah->rx[0].data_phys);
    CHECK((d0[3] & 0xfff) == ATH9K_RX_BUF_SIZE);
    CHECK(!ath9k_hw_rx_next(ah, &fr));

    complete_rx(ah, 0, 40, 30, true);
    CHECK(ath9k_hw_rx_next(ah, &fr));
    CHECK(fr.len == 40 && fr.rssi == 30 && fr.rate == 0x0b && fr.data == ah->rx[0].data);
    CHECK(!ath9k_hw_rx_next(ah, &fr));

    complete_rx(ah, 1, 60, 20, false);
    complete_rx(ah, 2, 50, 25, true);
    CHECK(ath9k_hw_rx_next(ah, &fr));
    CHECK(fr.len == 50 && fr.rssi == 25);
    CHECK(((volatile uint32_t *)ah->rx[ah->rx_last].desc)[0] == 0);
    CHECK(!ath9k_hw_rx_next(ah, &fr));

    ath9k_hw_rx_stop(ah);
    CHECK(!ah->rx_running);
}

static void test_tx(struct ath_hw *ah)
{
    uint8_t frame[40];
    volatile uint32_t *ds;
    uint8_t idx;

    memset(frame, 0xA5, sizeof(frame));
    CHECK(ath9k_hw_tx_init(ah) == 0);
    idx = ah->tx_next;
    CHECK(ath9k_hw_tx_frame(ah, frame, sizeof(frame), ATH9K_HWRATE_1M, false));
    CHECK(last_txdp == ah->tx[idx].desc_phys);
    CHECK(last_txe == 1);
    ds = (volatile uint32_t *)(ah->tx_ring + (size_t)idx * ATH9K_DESC_STRIDE);
    CHECK(ds[0] == 0);
    CHECK(ds[1] == ah->tx[idx].buf_phys);
    CHECK((ds[2] & AR_FrameLen) == sizeof(frame) + 4);
    CHECK(MS(ds[2], AR_XmitPower0) == ATH9K_MAX_RATE_POWER);
    CHECK((ds[3] & 0xfff) == sizeof(frame));
    CHECK(MS(ds[3], AR_FrameType) == 0 && !(ds[3] & AR_NoAck));
    CHECK(MS(ds[4], AR_XmitDataTries0) == 4);
    CHECK(MS(ds[5], AR_XmitRate0) == ATH9K_HWRATE_1M);
    CHECK(memcmp(ah->tx[idx].buf, frame, sizeof(frame)) == 0);
    CHECK(ah->tx[idx].pending);

    ds[10 + 9] = 0;
    ath9k_hw_tx_reap(ah);
    CHECK(ah->tx[idx].pending);
    ds[10 + 1] = AR_FrmXmitOK;
    ds[10 + 9] = AR_TxDone;
    ath9k_hw_tx_reap(ah);
    CHECK(!ah->tx[idx].pending && ah->tx_ok == 1 && ah->tx_fail == 0);

    CHECK(ath9k_hw_tx_frame(ah, frame, sizeof(frame), ATH9K_HWRATE_11M, true));
    idx = (uint8_t)((idx + 1) % ATH9K_TX_RING);
    ds = (volatile uint32_t *)(ah->tx_ring + (size_t)idx * ATH9K_DESC_STRIDE);
    CHECK((ds[3] & AR_NoAck) && MS(ds[4], AR_XmitDataTries0) == 1);
    ds[10 + 9] = AR_TxDone;
    ath9k_hw_tx_reap(ah);
    CHECK(ah->tx_fail == 1);

    CHECK(!ath9k_hw_tx_frame(ah, frame, 0, ATH9K_HWRATE_1M, false));
    CHECK(!ath9k_hw_tx_frame(ah, frame, ATH9K_MAX_TX_LEN + 1, ATH9K_HWRATE_1M, false));
}

static void test_initvals(void)
{
    CHECK(AR9285MODES_9285_1_2_ROWS == 303);
    CHECK(AR9285COMMON_9285_1_2_ROWS == 317);
    CHECK(AR9285MODES_ORIGINAL_TX_GAIN_9285_1_2_ROWS == 38);
    CHECK(ar9285Modes_9285_1_2[0][0] == 0x00001030);
}

int main(void)
{
    static struct ath_hw ah;

    regs_mem = calloc(1, REG_SPACE);
    CHECK(regs_mem);
    CHECK(sizeof(struct ath9k_eeprom_4k) % 2 == 0);

    test_delta_slope();
    test_interpolation();
    test_initvals();
    test_srev_decoding(&ah);
    test_eeprom_and_mac(&ah);
    test_reset(&ah);
    test_mac_registers(&ah);
    test_rx_ring(&ah);
    test_tx(&ah);
    puts("ath9k tests passed");
    return 0;
}
