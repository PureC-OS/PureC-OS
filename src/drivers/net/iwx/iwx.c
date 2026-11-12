/*	Derived from OpenBSD: if_iwx.c,v 1.230 2026/09/19 19:36:42 stsp Exp	*/

/*
 * Copyright (c) 2014, 2016 genua gmbh <info@genua.de>
 *   Author: Stefan Sperling <stsp@openbsd.org>
 * Copyright (c) 2014 Fixup Software Ltd.
 * Copyright (c) 2017, 2019, 2020 Stefan Sperling <stsp@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
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

/*-
 * Based on BSD-licensed source modules in the Linux iwlwifi driver,
 * which were used as the reference documentation for this implementation.
 */

#include "iwx_compat.h"
#include "iwx.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "drivers/pci/pci.h"
#include "arch/x86_64/mmio.h"
#include "mm/pmm.h"
#include "drivers/interrupts/timer.h"
#include "kernel/process/scheduler.h"
#include "kernel/diagnostics/klog.h"
#include "lib/string.h"
#include "net/802.11/include/802.h"
#include "net/core/net_device.h"
#include "net/wifi/wifi.h"
#include "boot/install_source.h"
#include "fs/vfs.h"

#define IWX_DEVICE_FAMILY_AX210 2
#define IWX_CFG_MAC_TYPE_SO 0x37
#define IWX_CFG_RF_TYPE_HR2 0x10a
#define IWX_TX_RING_COUNT IWX_DEFAULT_QUEUE_SIZE
#define IWX_RX_MQ_RING_COUNT 512
#define IWX_RBUF_SIZE 4096
#define IWX_CMD_RESP_MAX 4096
#define IWX_INIT_COMPLETE 0x01
#define IWX_PNVM_COMPLETE 0x04
#define IWX_UCODE_SECT_MAX 69
#define IWX_MAX_FW_CMD_VERSIONS 704
#define IWX_FW_ADDR_CACHE_CONTROL 0xC0000000
#define IWX_TXDATA_FLAG_CMD_IS_NARROW 0x01
#define IWX_PAGE_SIZE 4096
#define IWX_FW_NAME "iwx-so-a0-hr-b0-77"
#define IWX_FW_PATH "/lib/firmware/" IWX_FW_NAME
#define IWX_FW_MAX_SIZE (4u * 1024u * 1024u)
#define IWX_NUM_2GHZ_CHANNELS 14
#define IWX_NUM_5GHZ_CHANNELS 37
#define IWX_NCHAN (IWX_NUM_2GHZ_CHANNELS + IWX_NUM_5GHZ_CHANNELS)
#define IWX_MAX_BSS 64
#define IWX_SCAN_DWELL_ACTIVE 10
#define IWX_SCAN_DWELL_PASSIVE 110
#define IWX_SCAN_ADWELL_MAX_BUDGET_FULL_SCAN 300
#define IWX_SCAN_ADWELL_DEFAULT_HB_N_APS 8
#define IWX_SCAN_ADWELL_DEFAULT_LB_N_APS 2
#define IWX_SCAN_ADWELL_DEFAULT_N_APS_SOCIAL 10
#define IWX_SCAN_ADWELL_N_APS_GO_FRIENDLY 10
#define IWX_SCAN_ADWELL_N_APS_SOCIAL_CHS 2

struct iwx_bss {
    uint8_t bssid[6];
    uint8_t ssid_len;
    char ssid[33];
    uint8_t channel;
    int8_t rssi;
    uint16_t capinfo;
    uint8_t rsn;
    uint8_t wpa;
    uint32_t seen;
    uint8_t rsn_len;
    uint8_t rsn_ie[64];
    uint8_t rates_len;
    uint8_t rates[16];
    uint16_t bintval;
    uint8_t dtim_count;
    uint8_t dtim_period;
    uint64_t tsf;
    uint32_t gp2;
};

#ifndef IWX_FW_CTXT_INVALID
#define IWX_FW_CTXT_INVALID 0xffffffffu
#endif
#define IWX_MQ_ID 1
#define IWX_SWQ_SLOTS 64
#define IWX_SWQ_BYTES 2048

struct iwx_swq {
    volatile uint32_t head;
    volatile uint32_t tail;
    uint16_t len[IWX_SWQ_SLOTS];
    uint8_t data[IWX_SWQ_SLOTS][IWX_SWQ_BYTES];
};

static struct iwx_swq iwx_txsw, iwx_rxsw;

static bool iwx_swq_put(struct iwx_swq *q, const uint8_t *p, uint32_t len)
{
    uint32_t h = q->head;
    if (h - q->tail >= IWX_SWQ_SLOTS || len > IWX_SWQ_BYTES)
        return false;
    uint32_t i = h % IWX_SWQ_SLOTS;
    for (uint32_t k = 0; k < len; k++)
        q->data[i][k] = p[k];
    q->len[i] = (uint16_t)len;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    q->head = h + 1;
    return true;
}

static const uint8_t *iwx_swq_peek(struct iwx_swq *q, uint32_t *len)
{
    uint32_t t = q->tail;
    if (t == q->head)
        return 0;
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    uint32_t i = t % IWX_SWQ_SLOTS;
    *len = q->len[i];
    return q->data[i];
}

static void iwx_swq_pop(struct iwx_swq *q)
{
    __atomic_thread_fence(__ATOMIC_RELEASE);
    q->tail = q->tail + 1;
}

static void iwx_swq_reset(struct iwx_swq *q)
{
    q->tail = q->head;
}
#define IWX_STATION_ID 0
#define IWX_MQ_BUF 1024
#define IWX_EXP2(x) ((1 << (x)) - 1)

static const uint8_t iwx_nvm_channels_8000[IWX_NCHAN] = {
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
    36, 40, 44, 48, 52, 56, 60, 64, 68, 72, 76, 80, 84, 88, 92,
    96, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144,
    149, 153, 157, 161, 165, 169, 173, 177, 181,
};
#define IWX_UREG_UMAC_CURRENT_PC 0xa05c18
#define IWX_UREG_LMAC1_CURRENT_PC 0xa05c1c
#define IWX_PCI_COMMAND_INTX_DISABLE (1u << 10)
#define IWX_ETIMEDOUT 60
#define IWX_EINVAL 22
#define IWX_ENOMEM 12
#define IWX_EBUSY 16
#define IWX_EIO 5
#define IWX_ENXIO 6
#define IWX_EHANDSHAKE 200

struct iwx_dma {
    void *va;
    uint64_t pa;
    size_t size;
};

struct iwx_fw_onesect {
    uint32_t devoff;
    const uint8_t *data;
    uint32_t len;
};

struct iwx_sc {
    struct pci_device_info pci;
    bool found;
    uint16_t subsys_vendor;
    uint16_t subsys_device;
    uint8_t pcie_cap;
    volatile uint8_t *regs;
    uint64_t bar_pa;
    uint64_t bar_size;
    int nic_locks;
    uint32_t hw_rev_raw;
    uint32_t hw_rev;
    uint32_t hw_rf_id;
    uint32_t hw_crf_id;
    uint32_t hw_cnv_id;
    uint32_t umac_prph_offset;
    uint32_t mac_addr_from_csr;
    uint32_t max_tfd_queue_size;
    int integrated;
    int imr_enabled;
    int pm_support;
    int ltr_enabled;
    int rfkill;
    uint32_t intmask;

    const uint8_t *fw_raw;
    uint64_t fw_size;
    uint32_t fw_major, fw_minor, fw_api;
    struct iwx_fw_onesect sect[IWX_UCODE_SECT_MAX];
    int sect_count;
    uint32_t sect_totlen;
    int other_sect_count;
    const uint8_t *iml;
    uint32_t iml_len;
    struct iwx_fw_cmd_version cmd_versions[IWX_MAX_FW_CMD_VERSIONS];
    int n_cmd_versions;
    uint8_t ucode_api[howmany(IWX_NUM_UCODE_TLV_API, 8)];
    uint8_t enabled_capa[howmany(IWX_NUM_UCODE_TLV_CAPA, 8)];
    uint32_t phy_config;
    uint32_t tlv_umac_err;
    uint32_t tlv_lmac_err;
    uint32_t pnvm_tlv_len;

    struct iwx_dma ctxt_info;
    struct iwx_dma prph_scratch;
    struct iwx_dma prph_info;
    struct iwx_dma iml_dma;
    struct iwx_dma fw_dram[2 * IWX_MAX_DRAM_ENTRY];
    struct iwx_dma paging[IWX_MAX_DRAM_ENTRY];
    int lmac_cnt, umac_cnt, paging_cnt;

    struct iwx_dma rx_free;
    struct iwx_dma rx_stat;
    struct iwx_dma rx_used;
    uint64_t rxbuf_pa[IWX_RX_MQ_RING_COUNT];
    uint8_t *rxbuf_va[IWX_RX_MQ_RING_COUNT];
    int rx_cur;

    struct iwx_dma tx_desc;
    struct iwx_dma tx_bc;
    struct iwx_dma tx_cmd;
    int tx_qid;
    int tx_cur;
    int tx_cur_hw;
    int tx_queued;
    uint8_t tx_flags[IWX_TX_RING_COUNT];
    volatile uint8_t tx_done[IWX_TX_RING_COUNT];
    uint8_t *resp_buf[IWX_TX_RING_COUNT];
    uint32_t resp_cap[IWX_TX_RING_COUNT];
    uint32_t resp_got[IWX_TX_RING_COUNT];
    uint8_t resp_storage[IWX_CMD_RESP_MAX];
    int wait_idx;

    int alive_hw;
    int uc_intr;
    int uc_ok;
    uint32_t uc_lmac_err[2];
    uint32_t uc_umac_err;
    uint32_t uc_log_event;
    uint32_t sku_id[3];
    uint32_t init_complete;
    int fatal;
    int dead_logged;
    uint32_t int_seen;
    uint32_t fh_seen;
    uint32_t pkts;
    uint8_t mac[6];

    struct iwx_dma tx_big;
    struct iwx_dma mq_desc;
    struct iwx_dma mq_bc;
    struct iwx_dma mq_cmd;
    struct iwx_dma mq_buf;
    int mq_cur;
    int mq_cur_hw;
    int mq_tail_hw;
    int mq_enabled;
    volatile uint32_t mq_txresp;
    uint16_t mq_last_status;
    int rate_v2;
    int lmac_id;
    uint16_t assoc_aid;
    int c_phy, c_mac, c_bind, c_sta, c_sp, c_assoc;
    struct net_device net;
    volatile int req_scan;
    volatile int req_connect;
    volatile int req_disconnect;
    volatile int scan_ready;
    volatile int link_up;
    volatile int conn_failed;
    volatile int32_t conn_error;
    volatile int busy;
    volatile int link_lost;
    volatile int lost_report;
    uint16_t tx_seq;
    uint32_t data_logged;
    char req_ssid[33];
    char req_pass[65];
    volatile int sp_started;
    volatile int sp_ended;
    int mlme_on;
    struct dot11_mlme mlme;
    struct iwx_bss *ap;
    volatile int eapol_seen;
    uint16_t eapol_keyinfo;
    uint8_t eapol_nonce_head[4];
    uint32_t n_scan_channels;
    uint32_t nvm_tx_ant;
    uint32_t nvm_rx_ant;
    int nvm_5ghz;
    int nvm_lar;
    int nchan;
    uint8_t chan_num[IWX_NCHAN];
    uint8_t chan_band[IWX_NCHAN];
    uint8_t chan_passive[IWX_NCHAN];
    volatile int scan_done;
    uint32_t scan_beacons;
    int nbss;
    struct iwx_bss bss[IWX_MAX_BSS];
};

static struct iwx_sc iwx_softc;
static uint64_t iwx_tsc_per_us;
static bool iwx_started;

static inline uint64_t iwx_rdtsc(void)
{
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static inline void iwx_pause(void)
{
    __asm__ volatile("pause" ::: "memory");
}

static inline void iwx_mb(void)
{
    __asm__ volatile("mfence" ::: "memory");
}

static void iwx_calibrate(void)
{
    uint64_t t0 = timer_ticks();
    while (timer_ticks() == t0)
        iwx_pause();
    uint64_t c0 = iwx_rdtsc();
    uint64_t t1 = timer_ticks();
    while (timer_ticks() - t1 < 20)
        iwx_pause();
    uint64_t c1 = iwx_rdtsc();
    uint64_t ms = timer_ticks() - t1;
    if (ms == 0)
        ms = 20;
    iwx_tsc_per_us = (c1 - c0) / (ms * 1000);
    if (iwx_tsc_per_us < 100 || iwx_tsc_per_us > 20000) {
        klogf(KLOG_WARN, "iwx: tsc calibration odd (%llu/us), using 3000",
              (unsigned long long)iwx_tsc_per_us);
        iwx_tsc_per_us = 3000;
    }
    klogf(KLOG_INFO, "iwx: tsc %llu ticks/us", (unsigned long long)iwx_tsc_per_us);
}

static void DELAY(uint32_t us)
{
    uint64_t start = iwx_rdtsc();
    uint64_t want = (uint64_t)us * iwx_tsc_per_us;
    while (iwx_rdtsc() - start < want)
        iwx_pause();
}

static inline uint32_t iwx_read(struct iwx_sc *sc, uint32_t reg)
{
    return *(volatile uint32_t *)(sc->regs + reg);
}

static inline void iwx_write(struct iwx_sc *sc, uint32_t reg, uint32_t val)
{
    *(volatile uint32_t *)(sc->regs + reg) = val;
}

static inline void iwx_write_1(struct iwx_sc *sc, uint32_t reg, uint8_t val)
{
    *(volatile uint8_t *)(sc->regs + reg) = val;
}

static inline void iwx_setbits(struct iwx_sc *sc, uint32_t reg, uint32_t mask)
{
    iwx_write(sc, reg, iwx_read(sc, reg) | mask);
}

static inline void iwx_clrbits(struct iwx_sc *sc, uint32_t reg, uint32_t mask)
{
    iwx_write(sc, reg, iwx_read(sc, reg) & ~mask);
}

static bool iwx_dma_alloc(struct iwx_dma *dma, size_t size, const char *what)
{
    uint64_t pages = (size + IWX_PAGE_SIZE - 1) / IWX_PAGE_SIZE;
    if (pages == 0)
        pages = 1;
    uint64_t pa = pmm_allocate_contiguous(pages);
    if (!pa) {
        klogf(KLOG_ERROR, "iwx: dma alloc failed: %s (%llu bytes)", what,
              (unsigned long long)size);
        dma->va = NULL;
        dma->pa = 0;
        dma->size = 0;
        return false;
    }
    dma->pa = pa;
    dma->va = pmm_physical_to_virtual(pa);
    dma->size = size;
    memset(dma->va, 0, pages * IWX_PAGE_SIZE);
    return true;
}

static int iwx_poll_bit(struct iwx_sc *sc, uint32_t reg, uint32_t bits,
                        uint32_t mask, int timo)
{
    for (;;) {
        if ((iwx_read(sc, reg) & mask) == (bits & mask))
            return 1;
        if (timo < 10)
            return 0;
        timo -= 10;
        DELAY(10);
    }
}

static int iwx_nic_lock(struct iwx_sc *sc)
{
    if (sc->nic_locks > 0) {
        sc->nic_locks++;
        return 1;
    }
    iwx_setbits(sc, IWX_CSR_GP_CNTRL, IWX_CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);
    DELAY(2);
    if (iwx_poll_bit(sc, IWX_CSR_GP_CNTRL,
                     IWX_CSR_GP_CNTRL_REG_VAL_MAC_ACCESS_EN,
                     IWX_CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY |
                         IWX_CSR_GP_CNTRL_REG_FLAG_GOING_TO_SLEEP,
                     150000)) {
        sc->nic_locks++;
        return 1;
    }
    klogf(KLOG_ERROR, "iwx: acquiring device failed (GP_CNTRL=0x%08x)",
          iwx_read(sc, IWX_CSR_GP_CNTRL));
    return 0;
}

static void iwx_nic_unlock(struct iwx_sc *sc)
{
    if (sc->nic_locks > 0) {
        if (--sc->nic_locks == 0)
            iwx_clrbits(sc, IWX_CSR_GP_CNTRL,
                        IWX_CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);
    } else {
        klogf(KLOG_WARN, "iwx: NIC already unlocked");
    }
}

static void iwx_nic_assert_locked(struct iwx_sc *sc, uint32_t addr)
{
    if (sc->nic_locks <= 0)
        klogf(KLOG_ERROR, "iwx: prph 0x%x access without nic lock", addr);
}

static uint32_t iwx_read_prph_unlocked(struct iwx_sc *sc, uint32_t addr)
{
    iwx_write(sc, IWX_HBUS_TARG_PRPH_RADDR, ((addr & 0x00ffffff) | (3u << 24)));
    iwx_mb();
    return iwx_read(sc, IWX_HBUS_TARG_PRPH_RDAT);
}

static uint32_t iwx_read_prph(struct iwx_sc *sc, uint32_t addr)
{
    iwx_nic_assert_locked(sc, addr);
    return iwx_read_prph_unlocked(sc, addr);
}

static void iwx_write_prph_unlocked(struct iwx_sc *sc, uint32_t addr, uint32_t val)
{
    iwx_write(sc, IWX_HBUS_TARG_PRPH_WADDR, ((addr & 0x00ffffff) | (3u << 24)));
    iwx_mb();
    iwx_write(sc, IWX_HBUS_TARG_PRPH_WDAT, val);
}

static void iwx_write_prph(struct iwx_sc *sc, uint32_t addr, uint32_t val)
{
    iwx_nic_assert_locked(sc, addr);
    iwx_write_prph_unlocked(sc, addr, val);
}

static uint32_t iwx_read_umac_prph_unlocked(struct iwx_sc *sc, uint32_t addr)
{
    return iwx_read_prph_unlocked(sc, addr + sc->umac_prph_offset);
}

static uint32_t iwx_read_umac_prph(struct iwx_sc *sc, uint32_t addr)
{
    return iwx_read_prph(sc, addr + sc->umac_prph_offset);
}

static void iwx_write_umac_prph_unlocked(struct iwx_sc *sc, uint32_t addr, uint32_t val)
{
    iwx_write_prph_unlocked(sc, addr + sc->umac_prph_offset, val);
}

static void iwx_write_umac_prph(struct iwx_sc *sc, uint32_t addr, uint32_t val)
{
    iwx_write_prph(sc, addr + sc->umac_prph_offset, val);
}

static int iwx_read_mem(struct iwx_sc *sc, uint32_t addr, void *buf, int dwords)
{
    uint32_t *vals = buf;
    if (!iwx_nic_lock(sc))
        return IWX_EBUSY;
    iwx_write(sc, IWX_HBUS_TARG_MEM_RADDR, addr);
    for (int offs = 0; offs < dwords; offs++)
        vals[offs] = le32toh(iwx_read(sc, IWX_HBUS_TARG_MEM_RDAT));
    iwx_nic_unlock(sc);
    return 0;
}

static const struct {
    const char *name;
    uint8_t num;
} iwx_advanced_lookup[] = {
    {"NMI_INTERRUPT_WDG", 0x34},
    {"SYSASSERT", 0x35},
    {"UCODE_VERSION_MISMATCH", 0x37},
    {"BAD_COMMAND", 0x38},
    {"BAD_COMMAND", 0x39},
    {"NMI_INTERRUPT_DATA_ACTION_PT", 0x3C},
    {"FATAL_ERROR", 0x3D},
    {"NMI_TRM_HW_ERR", 0x46},
    {"NMI_INTERRUPT_TRM", 0x4C},
    {"NMI_INTERRUPT_BREAK_POINT", 0x54},
    {"NMI_INTERRUPT_WDG_RXF_FULL", 0x5C},
    {"NMI_INTERRUPT_WDG_NO_RBD_RXF_FULL", 0x64},
    {"NMI_INTERRUPT_HOST", 0x66},
    {"NMI_INTERRUPT_LMAC_FATAL", 0x70},
    {"NMI_INTERRUPT_UMAC_FATAL", 0x71},
    {"NMI_INTERRUPT_OTHER_LMAC_FATAL", 0x73},
    {"NMI_INTERRUPT_ACTION_PT", 0x7C},
    {"NMI_INTERRUPT_UNKNOWN", 0x84},
    {"NMI_INTERRUPT_INST_ACTION_PT", 0x86},
    {"ADVANCED_SYSASSERT", 0},
};

static const char *iwx_desc_lookup(uint32_t num)
{
    size_t i;
    for (i = 0; i < nitems(iwx_advanced_lookup) - 1; i++)
        if (iwx_advanced_lookup[i].num == (num & ~0xf0000000u))
            return iwx_advanced_lookup[i].name;
    return iwx_advanced_lookup[i].name;
}

struct iwx_error_event_table {
    uint32_t valid, error_id, trm_hw_status0, trm_hw_status1, blink2, ilink1,
        ilink2, data1, data2, data3, bcon_time, tsf_low, tsf_hi, gp1, gp2,
        fw_rev_type, major, minor, hw_ver, brd_ver, log_pc, frame_ptr,
        stack_ptr, hcmd, isr0, isr1, isr2, isr3, isr4, last_cmd_id, wait_event,
        l2p_control, l2p_duration, l2p_mhvalid, l2p_addr_match, lmpm_pmg_sel,
        u_timestamp, flow_handler;
} __packed;

struct iwx_umac_error_event_table {
    uint32_t valid, error_id, blink1, blink2, ilink1, ilink2, data1, data2,
        data3, umac_major, umac_minor, frame_pointer, stack_pointer,
        cmd_header, nic_isr_pref;
} __packed;

static void iwx_nic_umac_error(struct iwx_sc *sc)
{
    struct iwx_umac_error_event_table t;
    uint32_t base = sc->uc_umac_err;
    if (base < 0x400000) {
        klogf(KLOG_ERROR, "iwx: invalid UMAC error log pointer 0x%08x", base);
        return;
    }
    if (iwx_read_mem(sc, base, &t, sizeof(t) / sizeof(uint32_t))) {
        klogf(KLOG_ERROR, "iwx: reading UMAC errlog failed");
        return;
    }
    klogf(KLOG_ERROR, "iwx: UMAC error log @0x%08x valid=%u", base, t.valid);
    klogf(KLOG_ERROR, "iwx: 0x%08x | %s", t.error_id, iwx_desc_lookup(t.error_id));
    klogf(KLOG_ERROR, "iwx: umac blink1=%08x blink2=%08x ilink1=%08x ilink2=%08x",
          t.blink1, t.blink2, t.ilink1, t.ilink2);
    klogf(KLOG_ERROR, "iwx: umac data1=%08x data2=%08x data3=%08x",
          t.data1, t.data2, t.data3);
    klogf(KLOG_ERROR, "iwx: umac major=%08x minor=%08x fp=%08x sp=%08x",
          t.umac_major, t.umac_minor, t.frame_pointer, t.stack_pointer);
    klogf(KLOG_ERROR, "iwx: umac last host cmd=%08x isr=%08x",
          t.cmd_header, t.nic_isr_pref);
}

static void iwx_nic_error(struct iwx_sc *sc)
{
    struct iwx_error_event_table t;
    uint32_t base = sc->uc_lmac_err[0];
    klogf(KLOG_ERROR, "iwx: dumping device error log");
    if (base < 0x400000) {
        klogf(KLOG_ERROR, "iwx: invalid LMAC error log pointer 0x%08x", base);
    } else if (iwx_read_mem(sc, base, &t, sizeof(t) / sizeof(uint32_t))) {
        klogf(KLOG_ERROR, "iwx: reading LMAC errlog failed");
    } else if (!t.valid) {
        klogf(KLOG_ERROR, "iwx: LMAC errlog @0x%08x not valid", base);
    } else {
        klogf(KLOG_ERROR, "iwx: LMAC error log @0x%08x valid=%u", base, t.valid);
        klogf(KLOG_ERROR, "iwx: 0x%08x | %s", t.error_id, iwx_desc_lookup(t.error_id));
        klogf(KLOG_ERROR, "iwx: trm0=%08x trm1=%08x blink2=%08x ilink1=%08x ilink2=%08x",
              t.trm_hw_status0, t.trm_hw_status1, t.blink2, t.ilink1, t.ilink2);
        klogf(KLOG_ERROR, "iwx: data1=%08x data2=%08x data3=%08x bcon=%08x",
              t.data1, t.data2, t.data3, t.bcon_time);
        klogf(KLOG_ERROR, "iwx: tsf=%08x:%08x gp1=%08x gp2=%08x rev_type=%08x",
              t.tsf_hi, t.tsf_low, t.gp1, t.gp2, t.fw_rev_type);
        klogf(KLOG_ERROR, "iwx: major=%08x minor=%08x hw_ver=%08x brd_ver=%08x",
              t.major, t.minor, t.hw_ver, t.brd_ver);
        klogf(KLOG_ERROR, "iwx: log_pc=%08x fp=%08x sp=%08x hcmd=%08x",
              t.log_pc, t.frame_ptr, t.stack_ptr, t.hcmd);
        klogf(KLOG_ERROR, "iwx: isr0=%08x isr1=%08x isr2=%08x isr3=%08x isr4=%08x",
              t.isr0, t.isr1, t.isr2, t.isr3, t.isr4);
        klogf(KLOG_ERROR, "iwx: last_cmd=%08x wait_event=%08x l2p=%08x/%08x/%08x/%08x",
              t.last_cmd_id, t.wait_event, t.l2p_control, t.l2p_duration,
              t.l2p_mhvalid, t.l2p_addr_match);
        klogf(KLOG_ERROR, "iwx: pmg_sel=%08x timestamp=%08x flow_handler=%08x",
              t.lmpm_pmg_sel, t.u_timestamp, t.flow_handler);
    }
    if (sc->uc_umac_err)
        iwx_nic_umac_error(sc);
}

static void iwx_dump_state(struct iwx_sc *sc, const char *why)
{
    const struct iwx_prph_info *pi = sc->prph_info.va;
    uint16_t stat = sc->rx_stat.va ? *(volatile uint16_t *)sc->rx_stat.va : 0;
    klogf(KLOG_ERROR, "iwx: state (%s): CSR_INT=0x%08x FH_INT=0x%08x INT_MASK=0x%08x",
          why, iwx_read(sc, IWX_CSR_INT), iwx_read(sc, IWX_CSR_FH_INT_STATUS),
          iwx_read(sc, IWX_CSR_INT_MASK));
    klogf(KLOG_ERROR, "iwx: GP_CNTRL=0x%08x RESET=0x%08x HW_IF_CFG=0x%08x GP1=0x%08x",
          iwx_read(sc, IWX_CSR_GP_CNTRL), iwx_read(sc, IWX_CSR_RESET),
          iwx_read(sc, IWX_CSR_HW_IF_CONFIG_REG), iwx_read(sc, IWX_CSR_UCODE_DRV_GP1));
    klogf(KLOG_ERROR, "iwx: IML_RESP=0x%08x CTXT_ADDR=0x%08x:%08x IML_SIZE=0x%x",
          iwx_read(sc, IWX_CSR_IML_RESP_ADDR), iwx_read(sc, IWX_CSR_CTXT_INFO_ADDR + 4),
          iwx_read(sc, IWX_CSR_CTXT_INFO_ADDR), iwx_read(sc, IWX_CSR_IML_SIZE_ADDR));
    klogf(KLOG_ERROR, "iwx: rx stat=%u cur=%d tx cur=%d cur_hw=%d queued=%d ints_seen=0x%08x fh_seen=0x%08x pkts=%u",
          stat, sc->rx_cur, sc->tx_cur, sc->tx_cur_hw, sc->tx_queued, sc->int_seen,
          sc->fh_seen, sc->pkts);
    if (pi)
        klogf(KLOG_ERROR, "iwx: prph_info boot_stage=0x%08x ipc_status=0x%08x sleep=0x%08x",
              pi->boot_stage_mirror, pi->ipc_status_mirror, pi->sleep_notif);
    if (iwx_nic_lock(sc)) {
        klogf(KLOG_ERROR, "iwx: UMAC_PC=0x%08x LMAC1_PC=0x%08x LOAD_STATUS=0x%08x CPU_INIT_RUN=0x%08x",
              iwx_read_umac_prph(sc, IWX_UREG_UMAC_CURRENT_PC),
              iwx_read_umac_prph(sc, IWX_UREG_LMAC1_CURRENT_PC),
              iwx_read_umac_prph(sc, IWX_UREG_UCODE_LOAD_STATUS),
              iwx_read_umac_prph(sc, IWX_UREG_CPU_INIT_RUN));
        klogf(KLOG_ERROR, "iwx: UREG_CHICK=0x%08x RFH_GEN_STATUS=0x%08x RFH_DMA_CFG=0x%08x",
              iwx_read_umac_prph(sc, IWX_UREG_CHICK),
              iwx_read_umac_prph(sc, IWX_RFH_GEN_STATUS_GEN3),
              iwx_read_umac_prph(sc, IWX_RFH_RXF_DMA_CFG_GEN3));
        iwx_nic_unlock(sc);
    }
}

static uint8_t iwx_lookup_cmd_ver(struct iwx_sc *sc, uint8_t grp, uint8_t cmd)
{
    for (int i = 0; i < sc->n_cmd_versions; i++) {
        const struct iwx_fw_cmd_version *e = &sc->cmd_versions[i];
        if (e->group == grp && e->cmd == cmd)
            return e->cmd_ver;
    }
    return IWX_FW_CMD_VER_UNKNOWN;
}

static uint8_t iwx_lookup_notif_ver(struct iwx_sc *sc, uint8_t grp, uint8_t cmd)
{
    for (int i = 0; i < sc->n_cmd_versions; i++) {
        const struct iwx_fw_cmd_version *e = &sc->cmd_versions[i];
        if (e->group == grp && e->cmd == cmd)
            return e->notif_ver;
    }
    return IWX_FW_CMD_VER_UNKNOWN;
}

static int iwx_firmware_store_section(struct iwx_sc *sc, uint32_t tlv_type,
                                      const uint8_t *data, size_t dlen)
{
    if (dlen < sizeof(uint32_t))
        return IWX_EINVAL;
    if (tlv_type != IWX_UCODE_TLV_SEC_RT) {
        sc->other_sect_count++;
        return 0;
    }
    if (sc->sect_count >= IWX_UCODE_SECT_MAX)
        return IWX_EINVAL;
    struct iwx_fw_onesect *s = &sc->sect[sc->sect_count];
    memcpy(&s->devoff, data, sizeof(uint32_t));
    s->data = data + sizeof(uint32_t);
    s->len = (uint32_t)(dlen - sizeof(uint32_t));
    sc->sect_count++;
    sc->sect_totlen += s->len;
    return 0;
}

static int iwx_read_firmware(struct iwx_sc *sc)
{
    const struct iwx_tlv_ucode_header *uhdr = (const void *)sc->fw_raw;
    struct iwx_ucode_tlv tlv;
    uint32_t tlv_type = 0;
    int err = 0, ntlv = 0, nunknown = 0;

    if (sc->fw_size < sizeof(*uhdr) || uhdr->zero != 0 ||
        le32toh(uhdr->magic) != IWX_TLV_UCODE_MAGIC) {
        klogf(KLOG_ERROR, "iwx: invalid firmware %s (size %llu)", IWX_FW_NAME,
              (unsigned long long)sc->fw_size);
        return IWX_EINVAL;
    }
    char hr[65];
    memcpy(hr, uhdr->human_readable, 64);
    hr[64] = 0;
    sc->fw_major = IWX_UCODE_MAJOR(le32toh(uhdr->ver));
    sc->fw_minor = IWX_UCODE_MINOR(le32toh(uhdr->ver));
    sc->fw_api = IWX_UCODE_API(le32toh(uhdr->ver));
    klogf(KLOG_INFO, "iwx: firmware header '%s' ver=0x%08x build=%u", hr,
          le32toh(uhdr->ver), le32toh(uhdr->build));

    const uint8_t *data = uhdr->data;
    size_t len = sc->fw_size - sizeof(*uhdr);

    while (len >= sizeof(tlv)) {
        size_t tlv_len;
        const uint8_t *tlv_data;

        memcpy(&tlv, data, sizeof(tlv));
        tlv_len = le32toh(tlv.length);
        tlv_type = le32toh(tlv.type);
        len -= sizeof(tlv);
        data += sizeof(tlv);
        tlv_data = data;
        ntlv++;

        if (len < tlv_len) {
            klogf(KLOG_ERROR, "iwx: firmware too short: %llu bytes", (unsigned long long)len);
            err = IWX_EINVAL;
            goto parse_out;
        }

        switch (tlv_type) {
        case IWX_UCODE_TLV_NUM_OF_CPU: {
            uint32_t num_cpu;
            if (tlv_len != sizeof(uint32_t)) {
                err = IWX_EINVAL;
                goto parse_out;
            }
            memcpy(&num_cpu, tlv_data, 4);
            if (num_cpu < 1 || num_cpu > 2) {
                err = IWX_EINVAL;
                goto parse_out;
            }
            break;
        }
        case IWX_UCODE_TLV_SEC_RT:
        case IWX_UCODE_TLV_SEC_INIT:
        case IWX_UCODE_TLV_SEC_WOWLAN:
        case IWX_UCODE_TLV_SEC_RT_USNIFFER:
            err = iwx_firmware_store_section(sc, tlv_type, tlv_data, tlv_len);
            if (err)
                goto parse_out;
            break;
        case IWX_UCODE_TLV_PHY_SKU:
            if (tlv_len != sizeof(uint32_t)) {
                err = IWX_EINVAL;
                goto parse_out;
            }
            memcpy(&sc->phy_config, tlv_data, 4);
            break;
        case IWX_UCODE_TLV_N_SCAN_CHANNELS:
            if (tlv_len != sizeof(uint32_t)) {
                err = IWX_EINVAL;
                goto parse_out;
            }
            memcpy(&sc->n_scan_channels, tlv_data, 4);
            break;
        case IWX_UCODE_TLV_API_CHANGES_SET: {
            struct iwx_ucode_api api;
            if (tlv_len != sizeof(api)) {
                err = IWX_EINVAL;
                goto parse_out;
            }
            memcpy(&api, tlv_data, sizeof(api));
            uint32_t idx = le32toh(api.api_index);
            if (idx >= howmany(IWX_NUM_UCODE_TLV_API, 32)) {
                err = IWX_EINVAL;
                goto parse_out;
            }
            for (int i = 0; i < 32; i++)
                if (le32toh(api.api_flags) & (1u << i))
                    setbit(sc->ucode_api, i + 32 * (int)idx);
            break;
        }
        case IWX_UCODE_TLV_ENABLED_CAPABILITIES: {
            struct iwx_ucode_capa capa;
            if (tlv_len != sizeof(capa)) {
                err = IWX_EINVAL;
                goto parse_out;
            }
            memcpy(&capa, tlv_data, sizeof(capa));
            uint32_t idx = le32toh(capa.api_index);
            if (idx >= howmany(IWX_NUM_UCODE_TLV_CAPA, 32)) {
                err = IWX_EINVAL;
                goto parse_out;
            }
            for (int i = 0; i < 32; i++)
                if (le32toh(capa.api_capa) & (1u << i))
                    setbit(sc->enabled_capa, i + 32 * (int)idx);
            break;
        }
        case IWX_UCODE_TLV_FW_VERSION: {
            uint32_t v[3];
            if (tlv_len != sizeof(uint32_t) * 3) {
                err = IWX_EINVAL;
                goto parse_out;
            }
            memcpy(v, tlv_data, sizeof(v));
            sc->fw_major = le32toh(v[0]);
            sc->fw_minor = le32toh(v[1]);
            sc->fw_api = le32toh(v[2]);
            break;
        }
        case IWX_UCODE_TLV_UMAC_DEBUG_ADDRS: {
            struct iwx_umac_debug_addrs d;
            if (tlv_len != sizeof(d)) {
                err = IWX_EINVAL;
                goto parse_out;
            }
            memcpy(&d, tlv_data, sizeof(d));
            sc->tlv_umac_err = le32toh(d.error_info_addr) & ~IWX_FW_ADDR_CACHE_CONTROL;
            break;
        }
        case IWX_UCODE_TLV_LMAC_DEBUG_ADDRS: {
            struct iwx_lmac_debug_addrs d;
            if (tlv_len != sizeof(d)) {
                err = IWX_EINVAL;
                goto parse_out;
            }
            memcpy(&d, tlv_data, sizeof(d));
            sc->tlv_lmac_err = le32toh(d.error_event_table_ptr) & ~IWX_FW_ADDR_CACHE_CONTROL;
            break;
        }
        case IWX_UCODE_TLV_IML:
            sc->iml = tlv_data;
            sc->iml_len = (uint32_t)tlv_len;
            break;
        case IWX_UCODE_TLV_CMD_VERSIONS:
            if (tlv_len % sizeof(struct iwx_fw_cmd_version)) {
                tlv_len /= sizeof(struct iwx_fw_cmd_version);
                tlv_len *= sizeof(struct iwx_fw_cmd_version);
            }
            if (sc->n_cmd_versions != 0) {
                err = IWX_EINVAL;
                goto parse_out;
            }
            if (tlv_len > sizeof(sc->cmd_versions)) {
                err = IWX_EINVAL;
                goto parse_out;
            }
            memcpy(&sc->cmd_versions[0], tlv_data, tlv_len);
            sc->n_cmd_versions = (int)(tlv_len / sizeof(struct iwx_fw_cmd_version));
            break;
        case IWX_UCODE_TLV_PNVM_DATA:
            if (!sc->pnvm_tlv_len)
                sc->pnvm_tlv_len = (uint32_t)tlv_len;
            break;
        default:
            nunknown++;
            break;
        }

        if (roundup(tlv_len, 4) > len)
            break;
        len -= roundup(tlv_len, 4);
        data += roundup(tlv_len, 4);
    }

parse_out:
    if (err) {
        klogf(KLOG_ERROR, "iwx: firmware parse error %d, section type %u (0x%x)",
              err, tlv_type, tlv_type);
        return err;
    }
    if (sc->fw_major >= 35)
        klogf(KLOG_OK, "iwx: firmware version %u.%08x.%u", sc->fw_major,
              sc->fw_minor, sc->fw_api);
    else
        klogf(KLOG_OK, "iwx: firmware version %u.%u.%u", sc->fw_major,
              sc->fw_minor, sc->fw_api);
    klogf(KLOG_INFO, "iwx: tlvs=%d skipped=%d rt_sections=%d (%u bytes) other_sections=%d",
          ntlv, nunknown, sc->sect_count, sc->sect_totlen, sc->other_sect_count);
    klogf(KLOG_INFO, "iwx: iml=%u bytes cmd_versions=%d phy_sku=0x%08x pnvm_tlv=%u",
          sc->iml_len, sc->n_cmd_versions, sc->phy_config, sc->pnvm_tlv_len);
    klogf(KLOG_INFO, "iwx: tlv err ptrs umac=0x%08x lmac=0x%08x; alive notif ver=%u",
          sc->tlv_umac_err, sc->tlv_lmac_err,
          iwx_lookup_notif_ver(sc, IWX_LEGACY_GROUP, IWX_ALIVE));
    return 0;
}

static bool iwx_load_firmware_file(struct iwx_sc *sc)
{
    const void *addr = NULL;
    uint64_t size = 0;
    if (boot_get_module(IWX_FW_PATH, &addr, &size) && addr && size) {
        sc->fw_raw = addr;
        sc->fw_size = size;
        klogf(KLOG_INFO, "iwx: firmware from boot module %s (%llu bytes)", IWX_FW_PATH,
              (unsigned long long)size);
        return true;
    }
    klogf(KLOG_INFO, "iwx: no boot module %s, waiting for root fs", IWX_FW_PATH);
    for (int i = 0; i < 150 && !vfs_is_root_mounted(); i++)
        scheduler_sleep(200);
    if (!vfs_is_root_mounted()) {
        klogf(KLOG_ERROR, "iwx: root fs not mounted after 30 s");
        return false;
    }
    int32_t fd = vfs_open(IWX_FW_PATH);
    if (fd < 0) {
        klogf(KLOG_ERROR, "iwx: cannot open %s (%d)", IWX_FW_PATH, fd);
        return false;
    }
    uint64_t pa = pmm_allocate_contiguous(IWX_FW_MAX_SIZE / IWX_PAGE_SIZE);
    if (!pa) {
        klogf(KLOG_ERROR, "iwx: no memory for firmware buffer");
        vfs_close(fd);
        return false;
    }
    uint8_t *buf = pmm_physical_to_virtual(pa);
    uint64_t total = 0;
    for (;;) {
        uint32_t chunk = 65536;
        if (total + chunk > IWX_FW_MAX_SIZE)
            chunk = (uint32_t)(IWX_FW_MAX_SIZE - total);
        if (chunk == 0) {
            klogf(KLOG_ERROR, "iwx: firmware file larger than %u bytes", IWX_FW_MAX_SIZE);
            vfs_close(fd);
            return false;
        }
        int32_t n = vfs_read(fd, buf + total, chunk);
        if (n < 0) {
            klogf(KLOG_ERROR, "iwx: read error %d at %llu", n, (unsigned long long)total);
            vfs_close(fd);
            return false;
        }
        if (n == 0)
            break;
        total += (uint64_t)n;
    }
    vfs_close(fd);
    sc->fw_raw = buf;
    sc->fw_size = total;
    klogf(KLOG_INFO, "iwx: firmware from vfs %s (%llu bytes)", IWX_FW_PATH,
          (unsigned long long)total);
    return total > 0;
}

static int iwx_set_hw_ready(struct iwx_sc *sc)
{
    iwx_setbits(sc, IWX_CSR_HW_IF_CONFIG_REG, IWX_CSR_HW_IF_CONFIG_REG_BIT_NIC_READY);
    int ready = iwx_poll_bit(sc, IWX_CSR_HW_IF_CONFIG_REG,
                             IWX_CSR_HW_IF_CONFIG_REG_BIT_NIC_READY,
                             IWX_CSR_HW_IF_CONFIG_REG_BIT_NIC_READY, 50);
    if (ready)
        iwx_setbits(sc, IWX_CSR_MBOX_SET_REG, IWX_CSR_MBOX_SET_REG_OS_ALIVE);
    return ready;
}

static int iwx_prepare_card_hw(struct iwx_sc *sc)
{
    int t = 0;
    if (iwx_set_hw_ready(sc))
        return 0;
    iwx_setbits(sc, IWX_CSR_DBG_LINK_PWR_MGMT_REG, IWX_CSR_RESET_LINK_PWR_MGMT_DISABLED);
    DELAY(1000);
    for (int ntries = 0; ntries < 10; ntries++) {
        iwx_setbits(sc, IWX_CSR_HW_IF_CONFIG_REG, IWX_CSR_HW_IF_CONFIG_REG_PREPARE);
        do {
            if (iwx_set_hw_ready(sc))
                return 0;
            DELAY(200);
            t += 200;
        } while (t < 150000);
        DELAY(25000);
    }
    return IWX_ETIMEDOUT;
}

static uint32_t iwx_pci_cfg8(struct iwx_sc *sc, uint8_t off)
{
    uint32_t v = pci_read_config32(sc->pci.bus, sc->pci.slot, sc->pci.function,
                                   (uint8_t)(off & 0xfc));
    return (v >> ((off & 3) * 8)) & 0xff;
}

static uint8_t iwx_find_pcie_cap(struct iwx_sc *sc)
{
    uint32_t cmdsts = pci_read_config32(sc->pci.bus, sc->pci.slot, sc->pci.function, 0x04);
    if (!(cmdsts & (1u << 20)))
        return 0;
    uint8_t ptr = (uint8_t)(iwx_pci_cfg8(sc, 0x34) & 0xfc);
    for (int i = 0; i < 48 && ptr >= 0x40; i++) {
        if (iwx_pci_cfg8(sc, ptr) == 0x10)
            return ptr;
        ptr = (uint8_t)(iwx_pci_cfg8(sc, (uint8_t)(ptr + 1)) & 0xfc);
    }
    return 0;
}

static void iwx_apm_config(struct iwx_sc *sc)
{
    iwx_setbits(sc, IWX_CSR_GIO_REG, IWX_CSR_GIO_REG_VAL_L0S_DISABLED);
    if (!sc->pcie_cap)
        return;
    uint32_t lctl = pci_read_config32(sc->pci.bus, sc->pci.slot, sc->pci.function,
                                      (uint8_t)(sc->pcie_cap + 0x10));
    sc->pm_support = !(lctl & 0x1);
    uint32_t cap = pci_read_config32(sc->pci.bus, sc->pci.slot, sc->pci.function,
                                     (uint8_t)(sc->pcie_cap + 0x28));
    sc->ltr_enabled = (cap & 0x400) ? 1 : 0;
}

static int iwx_apm_init(struct iwx_sc *sc)
{
    iwx_setbits(sc, IWX_CSR_GIO_CHICKEN_BITS, IWX_CSR_GIO_CHICKEN_BITS_REG_BIT_L1A_NO_L0S_RX);
    iwx_setbits(sc, IWX_CSR_DBG_HPET_MEM_REG, IWX_CSR_DBG_HPET_MEM_REG_VAL);
    iwx_setbits(sc, IWX_CSR_HW_IF_CONFIG_REG, IWX_CSR_HW_IF_CONFIG_REG_BIT_HAP_WAKE_L1A);
    iwx_apm_config(sc);
    iwx_setbits(sc, IWX_CSR_GP_CNTRL, IWX_CSR_GP_CNTRL_REG_FLAG_INIT_DONE);
    uint32_t ready = IWX_CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY;
    if (!iwx_poll_bit(sc, IWX_CSR_GP_CNTRL, ready, ready, 25000)) {
        klogf(KLOG_ERROR, "iwx: timeout waiting for clock stabilization (GP_CNTRL=0x%08x)",
              iwx_read(sc, IWX_CSR_GP_CNTRL));
        klogf(KLOG_ERROR, "iwx: apm init error %d", IWX_ETIMEDOUT);
        return IWX_ETIMEDOUT;
    }
    return 0;
}

static void iwx_apm_stop(struct iwx_sc *sc)
{
    iwx_setbits(sc, IWX_CSR_DBG_LINK_PWR_MGMT_REG, IWX_CSR_RESET_LINK_PWR_MGMT_DISABLED);
    iwx_setbits(sc, IWX_CSR_HW_IF_CONFIG_REG,
                IWX_CSR_HW_IF_CONFIG_REG_PREPARE | IWX_CSR_HW_IF_CONFIG_REG_ENABLE_PME);
    DELAY(1000);
    iwx_clrbits(sc, IWX_CSR_DBG_LINK_PWR_MGMT_REG, IWX_CSR_RESET_LINK_PWR_MGMT_DISABLED);
    DELAY(5000);
    iwx_setbits(sc, IWX_CSR_RESET, IWX_CSR_RESET_REG_FLAG_STOP_MASTER);
    if (!iwx_poll_bit(sc, IWX_CSR_RESET, IWX_CSR_RESET_REG_FLAG_MASTER_DISABLED,
                      IWX_CSR_RESET_REG_FLAG_MASTER_DISABLED, 100))
        klogf(KLOG_WARN, "iwx: timeout waiting for master");
    iwx_clrbits(sc, IWX_CSR_GP_CNTRL, IWX_CSR_GP_CNTRL_REG_FLAG_INIT_DONE);
}

static void iwx_conf_msix_hw(struct iwx_sc *sc, int stopped)
{
    if (!stopped && iwx_nic_lock(sc)) {
        iwx_write_umac_prph(sc, IWX_UREG_CHICK, IWX_UREG_CHICK_MSI_ENABLE);
        klogf(KLOG_INFO, "iwx: UREG_CHICK <- MSI_ENABLE, readback 0x%08x",
              iwx_read_umac_prph(sc, IWX_UREG_CHICK));
        iwx_nic_unlock(sc);
    }
}

static void iwx_disable_interrupts(struct iwx_sc *sc)
{
    iwx_write(sc, IWX_CSR_INT_MASK, 0);
    iwx_write(sc, IWX_CSR_INT, ~0u);
    iwx_write(sc, IWX_CSR_FH_INT_STATUS, ~0u);
}

static int iwx_check_rfkill(struct iwx_sc *sc)
{
    uint32_t v = iwx_read(sc, IWX_CSR_GP_CNTRL);
    sc->rfkill = (v & IWX_CSR_GP_CNTRL_REG_FLAG_HW_RF_KILL_SW) == 0;
    return sc->rfkill;
}

static void iwx_enable_rfkill_int(struct iwx_sc *sc)
{
    sc->intmask = IWX_CSR_INT_BIT_RF_KILL;
    iwx_write(sc, IWX_CSR_INT_MASK, 0);
    iwx_setbits(sc, IWX_CSR_GP_CNTRL, IWX_CSR_GP_CNTRL_REG_FLAG_RFKILL_WAKE_L1A_EN);
}

static void iwx_sw_reset(struct iwx_sc *sc)
{
    iwx_setbits(sc, IWX_CSR_RESET, IWX_CSR_RESET_REG_FLAG_SW_RESET);
    DELAY(5000);
}

static int iwx_start_hw(struct iwx_sc *sc)
{
    int err = iwx_prepare_card_hw(sc);
    if (err) {
        klogf(KLOG_ERROR, "iwx: prepare_card_hw timed out (HW_IF_CFG=0x%08x)",
              iwx_read(sc, IWX_CSR_HW_IF_CONFIG_REG));
        return err;
    }
    iwx_sw_reset(sc);
    err = iwx_apm_init(sc);
    if (err)
        return err;
    iwx_conf_msix_hw(sc, 0);
    iwx_enable_rfkill_int(sc);
    iwx_check_rfkill(sc);
    klogf(KLOG_INFO, "iwx: start_hw ok GP_CNTRL=0x%08x rfkill=%d", iwx_read(sc, IWX_CSR_GP_CNTRL),
          sc->rfkill);
    return 0;
}

static void iwx_get_crf_id(struct iwx_sc *sc)
{
    uint32_t val = iwx_read_umac_prph_unlocked(sc, IWX_WFPM_CTRL_REG);
    val |= IWX_WFPM_AUX_CTL_AUX_IF_MAC_OWNER_MSK;
    iwx_write_umac_prph_unlocked(sc, IWX_WFPM_CTRL_REG, val);
    sc->hw_crf_id = iwx_read_prph_unlocked(sc, IWX_SD_REG_VER_GEN2);
    sc->hw_cnv_id = iwx_read_prph_unlocked(sc, IWX_CNVI_AUX_MISC_CHIP);
}

static void iwx_update_rx_desc(struct iwx_sc *sc, int idx)
{
    struct iwx_rx_transfer_desc *desc = sc->rx_free.va;
    desc[idx].rbid = htole16((uint16_t)(idx & 0xffff));
    desc[idx].addr = htole64(sc->rxbuf_pa[idx]);
}

static bool iwx_alloc_rx_ring(struct iwx_sc *sc)
{
    sc->rx_cur = 0;
    if (!iwx_dma_alloc(&sc->rx_free, sizeof(struct iwx_rx_transfer_desc) * IWX_RX_MQ_RING_COUNT,
                       "rx free ring"))
        return false;
    if (!iwx_dma_alloc(&sc->rx_stat, sizeof(uint16_t), "rx status"))
        return false;
    if (!iwx_dma_alloc(&sc->rx_used, sizeof(struct iwx_rx_completion_desc) * IWX_RX_MQ_RING_COUNT,
                       "rx used ring"))
        return false;
    for (int i = 0; i < IWX_RX_MQ_RING_COUNT; i++) {
        uint64_t pa = pmm_allocate_page();
        if (!pa) {
            klogf(KLOG_ERROR, "iwx: rx buffer %d alloc failed", i);
            return false;
        }
        sc->rxbuf_pa[i] = pa;
        sc->rxbuf_va[i] = pmm_physical_to_virtual(pa);
        memset(sc->rxbuf_va[i], 0, IWX_RBUF_SIZE);
        iwx_update_rx_desc(sc, i);
    }
    return true;
}

static bool iwx_alloc_tx_ring(struct iwx_sc *sc)
{
    sc->tx_qid = IWX_DQA_CMD_QUEUE;
    sc->tx_cur = 0;
    sc->tx_cur_hw = 0;
    sc->tx_queued = 0;
    if (!iwx_dma_alloc(&sc->tx_desc, IWX_TX_RING_COUNT * sizeof(struct iwx_tfh_tfd), "tx tfd ring"))
        return false;
    if (!iwx_dma_alloc(&sc->tx_bc, sizeof(struct iwx_gen3_bc_tbl_entry) *
                                       IWX_TFD_QUEUE_BC_SIZE_GEN3_AX210, "tx bc table"))
        return false;
    if (!iwx_dma_alloc(&sc->tx_cmd, IWX_TX_RING_COUNT * sizeof(struct iwx_device_cmd), "tx cmd"))
        return false;
    return true;
}

static void iwx_reset_tx_ring(struct iwx_sc *sc)
{
    memset(sc->tx_bc.va, 0, sc->tx_bc.size);
    memset(sc->tx_desc.va, 0, sc->tx_desc.size);
    sc->tx_queued = 0;
    sc->tx_cur = 0;
    sc->tx_cur_hw = 0;
    for (int i = 0; i < IWX_TX_RING_COUNT; i++) {
        sc->tx_done[i] = 0;
        sc->resp_buf[i] = NULL;
    }
}

static int iwx_get_num_sections(struct iwx_sc *sc, int start)
{
    int i = 0;
    while (start < sc->sect_count &&
           sc->sect[start].devoff != IWX_CPU1_CPU2_SEPARATOR_SECTION &&
           sc->sect[start].devoff != IWX_PAGING_SEPARATOR_SECTION) {
        start++;
        i++;
    }
    return i;
}

static int iwx_ctxt_info_alloc_dma(const struct iwx_fw_onesect *sec, struct iwx_dma *dram)
{
    if (dram->va == NULL && !iwx_dma_alloc(dram, sec->len, "fw section"))
        return IWX_ENOMEM;
    memcpy(dram->va, sec->data, sec->len);
    return 0;
}

static int iwx_init_fw_sec(struct iwx_sc *sc, struct iwx_context_info_dram *ctxt_dram)
{
    int i, ret, fw_cnt = 0;

    sc->lmac_cnt = iwx_get_num_sections(sc, 0);
    sc->umac_cnt = iwx_get_num_sections(sc, sc->lmac_cnt + 1);
    sc->paging_cnt = iwx_get_num_sections(sc, sc->lmac_cnt + sc->umac_cnt + 2);
    klogf(KLOG_INFO, "iwx: fw sections lmac=%d umac=%d paging=%d (total %d)",
          sc->lmac_cnt, sc->umac_cnt, sc->paging_cnt, sc->sect_count);
    if (sc->lmac_cnt > IWX_MAX_DRAM_ENTRY || sc->umac_cnt > IWX_MAX_DRAM_ENTRY ||
        sc->paging_cnt > IWX_MAX_DRAM_ENTRY || sc->lmac_cnt == 0 || sc->umac_cnt == 0) {
        klogf(KLOG_ERROR, "iwx: bad section layout");
        return IWX_EINVAL;
    }
    if (sc->lmac_cnt < sc->sect_count)
        klogf(KLOG_INFO, "iwx: separator[%d]=0x%08x", sc->lmac_cnt, sc->sect[sc->lmac_cnt].devoff);
    if (sc->lmac_cnt + sc->umac_cnt + 1 < sc->sect_count)
        klogf(KLOG_INFO, "iwx: separator[%d]=0x%08x", sc->lmac_cnt + sc->umac_cnt + 1,
              sc->sect[sc->lmac_cnt + sc->umac_cnt + 1].devoff);

    for (i = 0; i < sc->lmac_cnt; i++) {
        ret = iwx_ctxt_info_alloc_dma(&sc->sect[i], &sc->fw_dram[fw_cnt]);
        if (ret)
            return ret;
        ctxt_dram->lmac_img[i] = htole64(sc->fw_dram[fw_cnt].pa);
        klogf(KLOG_DEBUG, "iwx: LMAC sect %d devoff 0x%08x at 0x%llx size %u", i,
              sc->sect[i].devoff, (unsigned long long)sc->fw_dram[fw_cnt].pa, sc->sect[i].len);
        fw_cnt++;
    }
    for (i = 0; i < sc->umac_cnt; i++) {
        ret = iwx_ctxt_info_alloc_dma(&sc->sect[fw_cnt + 1], &sc->fw_dram[fw_cnt]);
        if (ret)
            return ret;
        ctxt_dram->umac_img[i] = htole64(sc->fw_dram[fw_cnt].pa);
        klogf(KLOG_DEBUG, "iwx: UMAC sect %d devoff 0x%08x at 0x%llx size %u", i,
              sc->sect[fw_cnt + 1].devoff, (unsigned long long)sc->fw_dram[fw_cnt].pa,
              sc->sect[fw_cnt + 1].len);
        fw_cnt++;
    }
    for (i = 0; i < sc->paging_cnt; i++) {
        int fw_idx = fw_cnt + i + 2;
        ret = iwx_ctxt_info_alloc_dma(&sc->sect[fw_idx], &sc->paging[i]);
        if (ret)
            return ret;
        ctxt_dram->virtual_img[i] = htole64(sc->paging[i].pa);
        klogf(KLOG_DEBUG, "iwx: paging sect %d devoff 0x%08x at 0x%llx size %u", i,
              sc->sect[fw_idx].devoff, (unsigned long long)sc->paging[i].pa, sc->sect[fw_idx].len);
    }
    return 0;
}

static int iwx_ctxt_info_gen3_init(struct iwx_sc *sc)
{
    struct iwx_context_info_gen3 *ci;
    struct iwx_prph_scratch *ps;
    struct iwx_prph_scratch_ctrl_cfg *psc;
    uint16_t cb_size;
    uint32_t control_flags, scratch_size;
    uint64_t paddr;
    int err;

    if (sc->iml == NULL || sc->iml_len == 0) {
        klogf(KLOG_ERROR, "iwx: no image loader found in firmware file");
        return IWX_EINVAL;
    }
    if (sc->iml_dma.va == NULL && !iwx_dma_alloc(&sc->iml_dma, sc->iml_len, "iml"))
        return IWX_ENOMEM;

    ps = sc->prph_scratch.va;
    memset(ps, 0, sizeof(*ps));
    psc = &ps->ctrl_cfg;
    psc->version.version = 0;
    psc->version.mac_id = htole16((uint16_t)iwx_read(sc, IWX_CSR_HW_REV));
    psc->version.size = htole16(sizeof(*ps) / 4);

    control_flags = IWX_PRPH_SCRATCH_RB_SIZE_4K | IWX_PRPH_SCRATCH_MTR_MODE |
                    (IWX_PRPH_MTR_FORMAT_256B & IWX_PRPH_SCRATCH_MTR_FORMAT);
    if (sc->imr_enabled)
        control_flags |= IWX_PRPH_SCRATCH_IMR_DEBUG_EN;
    psc->control.control_flags = htole32(control_flags);
    psc->rbd_cfg.free_rbd_addr = htole64(sc->rx_free.pa);

    err = iwx_init_fw_sec(sc, &ps->dram);
    if (err)
        return err;

    ci = sc->ctxt_info.va;
    memset(ci, 0, sizeof(*ci));
    ci->prph_info_base_addr = htole64(sc->prph_info.pa);
    ci->prph_scratch_base_addr = htole64(sc->prph_scratch.pa);
    scratch_size = sizeof(*ps);
    ci->prph_scratch_size = htole32(scratch_size);
    ci->cr_head_idx_arr_base_addr = htole64(sc->rx_stat.pa);
    ci->tr_tail_idx_arr_base_addr = htole64(sc->prph_info.pa + IWX_PAGE_SIZE / 2);
    ci->cr_tail_idx_arr_base_addr = htole64(sc->prph_info.pa + 3 * IWX_PAGE_SIZE / 4);
    ci->mtr_base_addr = htole64(sc->tx_desc.pa);
    ci->mcr_base_addr = htole64(sc->rx_used.pa);
    cb_size = IWX_TFD_QUEUE_CB_SIZE(IWX_TX_RING_COUNT);
    ci->mtr_size = htole16(cb_size);
    cb_size = IWX_RX_QUEUE_CB_SIZE(IWX_MQ_RX_TABLE_SIZE);
    ci->mcr_size = htole16(cb_size);

    memcpy(sc->iml_dma.va, sc->iml, sc->iml_len);

    klogf(KLOG_INFO, "iwx: ctxt_info_gen3 @0x%llx (%u bytes) prph_scratch @0x%llx (%u bytes) flags 0x%08x mac_id 0x%04x",
          (unsigned long long)sc->ctxt_info.pa, (unsigned)sizeof(*ci),
          (unsigned long long)sc->prph_scratch.pa, scratch_size, control_flags,
          psc->version.mac_id);
    klogf(KLOG_INFO, "iwx: mtr @0x%llx size %u, mcr @0x%llx size %u, free rbd @0x%llx, stat @0x%llx, prph_info @0x%llx",
          (unsigned long long)sc->tx_desc.pa, ci->mtr_size, (unsigned long long)sc->rx_used.pa,
          ci->mcr_size, (unsigned long long)sc->rx_free.pa, (unsigned long long)sc->rx_stat.pa,
          (unsigned long long)sc->prph_info.pa);
    klogf(KLOG_INFO, "iwx: iml @0x%llx %u bytes", (unsigned long long)sc->iml_dma.pa, sc->iml_len);

    iwx_mb();

    paddr = sc->ctxt_info.pa;
    iwx_write(sc, IWX_CSR_CTXT_INFO_ADDR, (uint32_t)(paddr & 0xffffffff));
    iwx_write(sc, IWX_CSR_CTXT_INFO_ADDR + 4, (uint32_t)(paddr >> 32));

    paddr = sc->iml_dma.pa;
    iwx_write(sc, IWX_CSR_IML_DATA_ADDR, (uint32_t)(paddr & 0xffffffff));
    iwx_write(sc, IWX_CSR_IML_DATA_ADDR + 4, (uint32_t)(paddr >> 32));
    iwx_write(sc, IWX_CSR_IML_SIZE_ADDR, sc->iml_len);

    iwx_setbits(sc, IWX_CSR_CTXT_INFO_BOOT_CTRL, IWX_CSR_AUTO_FUNC_BOOT_ENA);

    if (!iwx_nic_lock(sc))
        return IWX_EBUSY;
    iwx_write_umac_prph(sc, IWX_UREG_CPU_INIT_RUN, 1);
    iwx_nic_unlock(sc);
    klogf(KLOG_INFO, "iwx: firmware self-load kicked (CPU_INIT_RUN=1)");
    return 0;
}

static void iwx_cmd_done(struct iwx_sc *sc, int qid, int idx, uint32_t code)
{
    if (qid != IWX_DQA_CMD_QUEUE)
        return;
    if (idx < 0 || idx >= IWX_TX_RING_COUNT)
        return;
    sc->tx_done[idx] = 1;
    if (sc->tx_queued == 0)
        klogf(KLOG_WARN, "iwx: unexpected firmware response to command 0x%x", code);
    else if (sc->tx_queued > 0)
        sc->tx_queued--;
}

static int iwx_rx_pkt_valid(const struct iwx_rx_packet *pkt)
{
    int qid = pkt->hdr.qid & ~0x80;
    int idx = pkt->hdr.idx;
    int code = IWX_WIDE_ID(pkt->hdr.flags, pkt->hdr.code);
    return (!(qid == 0 && idx == 0 && code == 0) &&
            pkt->len_n_flags != htole32(IWX_FH_RSCSR_FRAME_INVALID));
}

static void iwx_rx_alive(struct iwx_sc *sc, const struct iwx_rx_packet *pkt)
{
    int notif_ver = iwx_lookup_notif_ver(sc, IWX_LEGACY_GROUP, IWX_ALIVE);
    uint32_t plen = iwx_rx_packet_payload_len(pkt);
    const struct iwx_lmac_alive *lmac = NULL;
    const struct iwx_umac_alive *umac = NULL;
    uint16_t status = 0, flags = 0;

    sc->uc_ok = 0;
    klogf(KLOG_INFO, "iwx: ALIVE notif ver %d payload %u (v4=%u v5=%u v6=%u)", notif_ver, plen,
          (unsigned)sizeof(struct iwx_alive_resp_v4), (unsigned)sizeof(struct iwx_alive_resp_v5),
          (unsigned)sizeof(struct iwx_alive_resp_v6));

    if (notif_ver == 6 || notif_ver == 7) {
        const struct iwx_alive_resp_v6 *r = (const void *)pkt->data;
        if (plen != sizeof(*r)) {
            klogf(KLOG_ERROR, "iwx: ALIVE v6 size mismatch");
            sc->uc_intr = 1;
            return;
        }
        status = r->status;
        flags = r->flags;
        lmac = r->lmac_data;
        umac = &r->umac_data;
        sc->sku_id[0] = le32toh(r->sku_id.data[0]);
        sc->sku_id[1] = le32toh(r->sku_id.data[1]);
        sc->sku_id[2] = le32toh(r->sku_id.data[2]);
        klogf(KLOG_INFO, "iwx: ALIVE imr base=0x%llx size=0x%x enabled=%u",
              (unsigned long long)r->imr.base_addr, r->imr.size, r->imr.enabled);
    } else if (notif_ver == 5) {
        const struct iwx_alive_resp_v5 *r = (const void *)pkt->data;
        if (plen != sizeof(*r)) {
            klogf(KLOG_ERROR, "iwx: ALIVE v5 size mismatch");
            sc->uc_intr = 1;
            return;
        }
        status = r->status;
        flags = r->flags;
        lmac = r->lmac_data;
        umac = &r->umac_data;
        sc->sku_id[0] = le32toh(r->sku_id.data[0]);
        sc->sku_id[1] = le32toh(r->sku_id.data[1]);
        sc->sku_id[2] = le32toh(r->sku_id.data[2]);
    } else if (plen == sizeof(struct iwx_alive_resp_v4)) {
        const struct iwx_alive_resp_v4 *r = (const void *)pkt->data;
        status = r->status;
        flags = r->flags;
        lmac = r->lmac_data;
        umac = &r->umac_data;
    } else {
        klogf(KLOG_ERROR, "iwx: ALIVE unknown layout");
    }

    if (lmac && umac) {
        sc->uc_lmac_err[0] = le32toh(lmac[0].dbg_ptrs.error_event_table_ptr);
        sc->uc_lmac_err[1] = le32toh(lmac[1].dbg_ptrs.error_event_table_ptr);
        sc->uc_log_event = le32toh(lmac[0].dbg_ptrs.log_event_table_ptr);
        sc->uc_umac_err = le32toh(umac->dbg_ptrs.error_info_addr);
        if (status == IWX_ALIVE_STATUS_OK)
            sc->uc_ok = 1;
        klogf(sc->uc_ok ? KLOG_OK : KLOG_ERROR, "iwx: ALIVE status 0x%04x flags 0x%04x", status, flags);
        klogf(KLOG_INFO, "iwx: lmac0 %u.%u type %u/%u mac %u, lmac1 %u.%u, umac %u.%u",
              lmac[0].ucode_major, lmac[0].ucode_minor, lmac[0].ver_type, lmac[0].ver_subtype,
              lmac[0].mac, lmac[1].ucode_major, lmac[1].ucode_minor, umac->umac_major,
              umac->umac_minor);
        klogf(KLOG_INFO, "iwx: err tables lmac0=0x%08x lmac1=0x%08x umac=0x%08x log=0x%08x",
              sc->uc_lmac_err[0], sc->uc_lmac_err[1], sc->uc_umac_err, sc->uc_log_event);
        klogf(KLOG_INFO, "iwx: sku_id %08x %08x %08x", sc->sku_id[0], sc->sku_id[1], sc->sku_id[2]);
    }
    sc->uc_intr = 1;
}

static void iwx_rx_tx_resp(struct iwx_sc *sc, const struct iwx_rx_packet *pkt, uint32_t len)
{
    const struct iwx_tx_resp *r = (const void *)pkt->data;
    int qid = pkt->hdr.qid & 0x7f;
    if (len < sizeof(pkt->len_n_flags) + sizeof(struct iwx_cmd_header) + sizeof(*r)) {
        klogf(KLOG_WARN, "iwx: short TX response %u", len);
        return;
    }
    uint16_t st = le16toh(r->status.status) & IWX_TX_STATUS_MSK;
    uint32_t ssn = 0;
    memcpy(&ssn, (const uint8_t *)&r->status + r->frame_count * sizeof(struct iwx_agg_tx_status), 4);
    sc->mq_last_status = st;
    if (qid == IWX_MQ_ID)
        sc->mq_tail_hw = (int)(le32toh(ssn) & 0xffff);
    sc->mq_txresp++;
    bool ok = st == IWX_TX_STATUS_SUCCESS || st == IWX_TX_STATUS_DIRECT_DONE;
    if (ok)
        sc->net.stats.tx_packets += sc->link_up ? 1 : 0;
    else if (sc->link_up)
        sc->net.stats.tx_errors++;
    if (ok && sc->link_up)
        return;
    klogf(ok ? KLOG_INFO : KLOG_WARN,
          "iwx: tx done q%d idx %d: status 0x%02x (%s) frames %u retries %u rts_fail %u rate 0x%08x ssn %u",
          qid, pkt->hdr.idx, st,
          st == IWX_TX_STATUS_SUCCESS ? "ok" : st == IWX_TX_STATUS_DIRECT_DONE ? "direct" : "FAILED",
          r->frame_count, r->failure_frame, r->failure_rts, le32toh(r->initial_rate), le32toh(ssn));
}

static struct iwx_bss *iwx_bss_lookup(struct iwx_sc *sc, const uint8_t *bssid)
{
    for (int i = 0; i < sc->nbss; i++)
        if (memcmp(sc->bss[i].bssid, bssid, 6) == 0)
            return &sc->bss[i];
    if (sc->nbss >= IWX_MAX_BSS)
        return NULL;
    struct iwx_bss *b = &sc->bss[sc->nbss++];
    memset(b, 0, sizeof(*b));
    memcpy(b->bssid, bssid, 6);
    b->rssi = -127;
    return b;
}

static uint8_t iwx_rxframe[IWX_RBUF_SIZE];

static void iwx_rx_bss(struct iwx_sc *sc, const uint8_t *frame, uint32_t len, int rssi,
                       uint8_t hwchan, uint32_t gp2)
{
    const struct ieee80211_frame *wh = (const void *)frame;
    uint8_t subtype = wh->i_fc[0] & 0xf0;
    size_t body_off = sizeof(struct ieee80211_frame);
    if (len < body_off + 12)
        return;
    const uint8_t *body = frame + body_off;
    size_t body_len = len - body_off;

    if (sc->scan_done < 2)
        sc->scan_beacons++;
    struct iwx_bss *b = iwx_bss_lookup(sc, wh->i_addr3);
    if (!b)
        return;
    b->seen++;
    if (rssi > b->rssi)
        b->rssi = (int8_t)rssi;
    memcpy(&b->tsf, body, 8);
    b->gp2 = gp2;
    b->bintval = (uint16_t)(body[8] | (body[9] << 8));
    b->capinfo = (uint16_t)(body[10] | (body[11] << 8));
    if (!b->channel)
        b->channel = hwchan;

    size_t p = 12;
    while (p + 2 <= body_len) {
        uint8_t id = body[p], elen = body[p + 1];
        if (p + 2 + elen > body_len)
            break;
        const uint8_t *e = body + p + 2;
        if (id == 0 && elen <= 32 && (b->ssid_len == 0 || subtype == 0x50)) {
            memcpy(b->ssid, e, elen);
            b->ssid[elen] = 0;
            b->ssid_len = elen;
        } else if (id == 3 && elen == 1) {
            b->channel = e[0];
        } else if (id == 5 && elen >= 2) {
            b->dtim_count = e[0];
            b->dtim_period = e[1];
        } else if (id == 48) {
            b->rsn = 1;
            if (elen + 2 <= (int)sizeof(b->rsn_ie)) {
                memcpy(b->rsn_ie, body + p, elen + 2);
                b->rsn_len = (uint8_t)(elen + 2);
            }
        } else if ((id == 1 || id == 50) && (id == 1 || b->rates_len + elen <= (int)sizeof(b->rates))) {
            if (id == 1)
                b->rates_len = 0;
            if (b->rates_len + elen <= (int)sizeof(b->rates)) {
                memcpy(b->rates + b->rates_len, e, elen);
                b->rates_len = (uint8_t)(b->rates_len + elen);
            }
        } else if (id == 221 && elen >= 4 && e[0] == 0x00 && e[1] == 0x50 && e[2] == 0xf2 &&
                   e[3] == 0x01) {
            b->wpa = 1;
        }
        p += 2 + (size_t)elen;
    }
}

static void iwx_rx_data(struct iwx_sc *sc, const uint8_t *frame, uint32_t len)
{
    const struct ieee80211_frame *wh = (const void *)frame;
    uint8_t fc0 = wh->i_fc[0], fc1 = wh->i_fc[1];
    if (!sc->ap || memcmp(wh->i_addr2, sc->ap->bssid, 6) != 0)
        return;
    if (fc0 & 0x40)
        return;
    uint32_t hdrlen = 24;
    if (fc0 & 0x80)
        hdrlen += 2;
    if ((fc1 & 0x03) == 0x03)
        hdrlen += 6;
    if (len < hdrlen + 8)
        return;
    const uint8_t *llc = frame + hdrlen;
    static const uint8_t eapol_snap[8] = {0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00, 0x88, 0x8e};
    if (memcmp(llc, eapol_snap, 8) != 0) {
        if (sc->link_up) {
            if (!iwx_swq_put(&iwx_rxsw, frame, len))
                sc->net.stats.rx_dropped++;
        } else if (sc->data_logged++ < 4) {
            klogf(KLOG_INFO, "iwx: data frame from AP fc 0x%02x%02x len %u before link up", fc1, fc0, len);
        }
        return;
    }
    const uint8_t *ep = llc + 8;
    uint32_t elen = len - hdrlen - 8;
    if (elen < 4 + 95) {
        klogf(KLOG_WARN, "iwx: short EAPOL frame %u bytes", elen);
        return;
    }
    uint16_t body_len = (uint16_t)(ep[2] << 8 | ep[3]);
    uint16_t keyinfo = (uint16_t)(ep[5] << 8 | ep[6]);
    sc->eapol_keyinfo = keyinfo;
    memcpy(sc->eapol_nonce_head, ep + 4 + 13, 4);
    sc->eapol_seen++;
    klogf(KLOG_OK, "iwx: EAPOL from AP: ver %u type %u len %u, descriptor %u key_info 0x%04x (%s%s%s%s), replay %02x%02x%02x%02x%02x%02x%02x%02x",
          ep[0], ep[1], body_len, ep[4], keyinfo, (keyinfo & 0x0008) ? "pairwise" : "group",
          (keyinfo & 0x0080) ? " ack" : "", (keyinfo & 0x0100) ? " mic" : "",
          (keyinfo & 0x0007) == 2 ? " hmac-sha1-aes" : "", ep[9], ep[10], ep[11], ep[12],
          ep[13], ep[14], ep[15], ep[16]);
}

static void iwx_rx_mpdu(struct iwx_sc *sc, const struct iwx_rx_packet *pkt, uint32_t pktlen)
{
    const size_t desc_size = sizeof(struct iwx_rx_mpdu_desc);
    const size_t hdr = sizeof(pkt->len_n_flags) + sizeof(struct iwx_cmd_header);
    if (pktlen < hdr + desc_size)
        return;
    size_t maxlen = pktlen - hdr - desc_size;
    const struct iwx_rx_mpdu_desc *desc = (const void *)pkt->data;
    uint32_t status = le32toh(desc->status);
    if (!(status & IWX_RX_MPDU_RES_STATUS_CRC_OK) ||
        !(status & IWX_RX_MPDU_RES_STATUS_OVERRUN_OK))
        return;
    uint32_t len = le16toh(desc->mpdu_len);
    if (len < sizeof(struct ieee80211_frame) || len > maxlen)
        return;
    const uint8_t *raw = pkt->data + desc_size;
    uint8_t fc0 = raw[0], fc1 = raw[1];
    uint8_t type = fc0 & 0x0c;
    uint32_t hdrlen = 24;
    if (type == 0x08) {
        if (fc0 & 0x80)
            hdrlen += 2;
        if ((fc1 & 0x03) == 0x03)
            hdrlen += 6;
    } else if (type != 0x00) {
        return;
    }
    const uint8_t *frame = raw;
    if (desc->mac_flags2 & IWX_RX_MPDU_MFLG2_PAD) {
        if (len < hdrlen + 2)
            return;
        memcpy(iwx_rxframe, raw, hdrlen);
        memcpy(iwx_rxframe + hdrlen, raw + hdrlen + 2, len - hdrlen - 2);
        len -= 2;
        frame = iwx_rxframe;
    }

    int sig_a = desc->v3.energy_a ? -(int)desc->v3.energy_a : -127;
    int sig_b = desc->v3.energy_b ? -(int)desc->v3.energy_b : -127;
    int rssi = sig_a > sig_b ? sig_a : sig_b;

    if (type == 0x08) {
        iwx_rx_data(sc, frame, len);
        return;
    }
    uint8_t subtype = fc0 & 0xf0;
    if (subtype == 0x80 || subtype == 0x50)
        iwx_rx_bss(sc, frame, len, rssi, desc->v3.channel, le32toh(desc->v3.gp2_on_air_rise));
    if (sc->mlme_on) {
        if (subtype != 0x80 && subtype != 0x50)
            klogf(KLOG_INFO, "iwx: mgmt rx subtype 0x%x len %u rssi %d", subtype >> 4, len, rssi);
        dot11_mlme_input(&sc->mlme, frame, (uint16_t)len, timer_ticks());
    }
}

static void iwx_rx_pkt(struct iwx_sc *sc, int ridx)
{
    uint8_t *buf = sc->rxbuf_va[ridx];
    uint32_t offset = 0;
    const size_t minsz = sizeof(uint32_t) + sizeof(struct iwx_cmd_header);

    while (offset + minsz < IWX_RBUF_SIZE) {
        struct iwx_rx_packet *pkt = (struct iwx_rx_packet *)(buf + offset);
        int qid = pkt->hdr.qid;
        int idx = pkt->hdr.idx;
        uint32_t code = IWX_WIDE_ID(pkt->hdr.flags, pkt->hdr.code);
        int handled = 1;

        if (!iwx_rx_pkt_valid(pkt)) {
            klogf(KLOG_WARN, "iwx: rx[%d] invalid packet len_n_flags=0x%08x", ridx,
                  pkt->len_n_flags);
            break;
        }
        if (iwx_cmd_groupid(code) == IWX_LONG_GROUP)
            code = iwx_cmd_opcode(code);
        uint32_t len = (uint32_t)sizeof(pkt->len_n_flags) + iwx_rx_packet_len(pkt);
        if (len < minsz || len > (IWX_RBUF_SIZE - offset)) {
            klogf(KLOG_WARN, "iwx: rx[%d] bad packet length %u", ridx, len);
            break;
        }
        sc->pkts++;
        if (code != IWX_REPLY_RX_MPDU_CMD && code != IWX_TX_CMD && code != IWX_STATISTICS_NOTIFICATION &&
            code != IWX_MISSED_BEACONS_NOTIFICATION &&
            code != IWX_WIDE_ID(IWX_DATA_PATH_GROUP, IWX_RX_NO_DATA_NOTIF) &&
            code != IWX_WIDE_ID(IWX_DATA_PATH_GROUP, 0xf8))
            klogf(KLOG_INFO, "iwx: rx[%d] pkt code 0x%x len %u qid 0x%x idx %d flags 0x%x", ridx,
                  code, len, qid, idx, pkt->hdr.flags);

        switch (code) {
        case IWX_REPLY_RX_MPDU_CMD:
            iwx_rx_mpdu(sc, pkt, len);
            break;
        case IWX_SCAN_ITERATION_COMPLETE_UMAC: {
            const struct iwx_umac_scan_iter_complete_notif *n = (const void *)pkt->data;
            klogf(KLOG_INFO, "iwx: scan iteration complete: %u channels, status %u, last channel %u",
                  n->scanned_channels, n->status, n->last_channel);
            if (sc->scan_done < 1)
                sc->scan_done = 1;
            break;
        }
        case IWX_SCAN_COMPLETE_UMAC: {
            const struct iwx_umac_scan_complete *n = (const void *)pkt->data;
            klogf(KLOG_OK, "iwx: scan complete: status %u ebs %u last_iter %u",
                  n->status, n->ebs_status, n->last_iter);
            sc->scan_done = 2;
            break;
        }
        case IWX_TX_CMD:
            iwx_rx_tx_resp(sc, pkt, len);
            break;
        case IWX_STATISTICS_NOTIFICATION:
        case IWX_WIDE_ID(IWX_DATA_PATH_GROUP, IWX_RX_NO_DATA_NOTIF):
        case IWX_WIDE_ID(IWX_DATA_PATH_GROUP, 0xf8):
            break;
        case IWX_MISSED_BEACONS_NOTIFICATION: {
            const struct iwx_missed_beacons_notif *m = (const void *)pkt->data;
            uint32_t missed = le32toh(m->consec_missed_beacons_since_last_rx);
            if (sc->link_up && !sc->link_lost && missed >= 20) {
                klogf(KLOG_WARN, "iwx: %u beacons missed in a row (%u/%u received), link lost", missed,
                      le32toh(m->num_recvd_beacons), le32toh(m->num_expected_beacons));
                sc->link_lost = 1;
            }
            break;
        }
        case IWX_WIDE_ID(IWX_MAC_CONF_GROUP, IWX_SESSION_PROTECTION_NOTIF): {
            const struct iwx_session_prot_notif *n = (const void *)pkt->data;
            klogf(KLOG_INFO, "iwx: session protection: mac %u status %u %s conf %u",
                  le32toh(n->mac_id), le32toh(n->status), le32toh(n->start) ? "start" : "end",
                  le32toh(n->conf_id));
            if (le32toh(n->start))
                sc->sp_started = 1;
            else
                sc->sp_ended = 1;
            break;
        }
        case IWX_WIDE_ID(IWX_DATA_PATH_GROUP, IWX_SCD_QUEUE_CONFIG_CMD):
        case IWX_WIDE_ID(IWX_DATA_PATH_GROUP, IWX_RLC_CONFIG_CMD):
        case IWX_WIDE_ID(IWX_MAC_CONF_GROUP, IWX_SESSION_PROTECTION_CMD):
        case IWX_PHY_CONTEXT_CMD:
        case IWX_MAC_CONTEXT_CMD:
        case IWX_BINDING_CONTEXT_CMD:
        case IWX_ADD_STA:
        case IWX_REMOVE_STA:
            break;
        case IWX_MCC_CHUB_UPDATE_CMD:
            klogf(KLOG_INFO, "iwx: MCC chub update notification");
            break;
        case IWX_ALIVE:
            iwx_rx_alive(sc, pkt);
            break;
        case IWX_MFUART_LOAD_NOTIFICATION:
        case IWX_DEBUG_LOG_MSG:
            break;
        case IWX_INIT_COMPLETE_NOTIF:
            sc->init_complete |= IWX_INIT_COMPLETE;
            klogf(KLOG_OK, "iwx: INIT_COMPLETE_NOTIF received");
            break;
        case IWX_WIDE_ID(IWX_REGULATORY_AND_NVM_GROUP, IWX_PNVM_INIT_COMPLETE):
            sc->init_complete |= IWX_PNVM_COMPLETE;
            klogf(KLOG_OK, "iwx: PNVM_INIT_COMPLETE received");
            break;
        case IWX_WIDE_ID(IWX_SYSTEM_GROUP, IWX_INIT_EXTENDED_CFG_CMD):
        case IWX_WIDE_ID(IWX_REGULATORY_AND_NVM_GROUP, IWX_NVM_ACCESS_COMPLETE):
        case IWX_WIDE_ID(IWX_REGULATORY_AND_NVM_GROUP, IWX_NVM_GET_INFO):
        case IWX_WIDE_ID(IWX_SYSTEM_GROUP, IWX_SOC_CONFIGURATION_CMD):
        case IWX_WIDE_ID(IWX_DATA_PATH_GROUP, IWX_DQA_ENABLE_CMD):
        case IWX_WIDE_ID(IWX_PHY_OPS_GROUP, IWX_TEMP_REPORTING_THRESHOLDS_CMD):
        case IWX_TX_ANT_CONFIGURATION_CMD:
        case IWX_BT_CONFIG:
        case IWX_LTR_CONFIG:
        case IWX_POWER_TABLE_CMD:
        case IWX_MCC_UPDATE_CMD:
        case IWX_SCAN_CFG_CMD:
        case IWX_SCAN_REQ_UMAC:
        case IWX_REPLY_BEACON_FILTERING_CMD:
            break;
        case IWX_REPLY_ERROR: {
            const struct iwx_error_resp *r = (const void *)pkt->data;
            klogf(KLOG_ERROR, "iwx: firmware error 0x%x, cmd 0x%x seq 0x%x service 0x%x",
                  le32toh(r->error_type), r->cmd_id, r->bad_cmd_seq_num, r->error_service);
            break;
        }
        default:
            handled = 0;
            klogf(KLOG_WARN, "iwx: unhandled firmware response 0x%x/0x%x rx ring %d[%d]", code,
                  pkt->len_n_flags, (qid & ~0x80), idx);
            break;
        }

        if (!(qid & (1 << 7)) && (qid & 0x7f) == IWX_DQA_CMD_QUEUE && idx >= 0 &&
            idx < IWX_TX_RING_COUNT && sc->resp_buf[idx]) {
            if ((pkt->hdr.flags & IWX_CMD_FAILED_MSK) || len < sizeof(*pkt) ||
                len > sc->resp_cap[idx]) {
                klogf(KLOG_WARN, "iwx: response 0x%x rejected flags 0x%x len %u cap %u",
                      code, pkt->hdr.flags, len, sc->resp_cap[idx]);
                sc->resp_buf[idx] = NULL;
            } else {
                memcpy(sc->resp_buf[idx], pkt, len);
                sc->resp_got[idx] = len;
            }
        }
        if (!(qid & (1 << 7)) && (qid & 0x7f) == IWX_DQA_CMD_QUEUE) {
            if (!handled)
                klogf(KLOG_WARN, "iwx: completing unhandled response 0x%x idx %d", code, idx);
            iwx_cmd_done(sc, qid, idx, code);
        }

        offset += roundup(len, IWX_FH_RSCSR_FRAME_ALIGN);
        break;
    }
}

static void iwx_notif_intr(struct iwx_sc *sc)
{
    uint16_t hw = le16toh(*(volatile uint16_t *)sc->rx_stat.va) & 0xfff;
    hw &= (IWX_RX_MQ_RING_COUNT - 1);
    if (sc->rx_cur == hw)
        return;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    while (sc->rx_cur != hw) {
        const struct iwx_rx_completion_desc *cd = sc->rx_used.va;
        uint16_t rbid = le16toh(cd[sc->rx_cur].rbid);
        if (rbid != sc->rx_cur)
            klogf(KLOG_WARN, "iwx: rx used[%d].rbid=%u mismatch", sc->rx_cur, rbid);
        iwx_rx_pkt(sc, sc->rx_cur);
        iwx_update_rx_desc(sc, sc->rx_cur);
        sc->rx_cur = (sc->rx_cur + 1) % IWX_RX_MQ_RING_COUNT;
    }
    hw = (hw == 0) ? IWX_RX_MQ_RING_COUNT - 1 : hw - 1;
    iwx_write(sc, IWX_RFH_Q0_FRBDCB_WIDX_TRG, hw & ~7);
}

static void iwx_post_rx_after_alive(struct iwx_sc *sc)
{
    for (int i = 0; i < IWX_RX_MQ_RING_COUNT; i++)
        iwx_update_rx_desc(sc, i);
    iwx_mb();
    iwx_write(sc, IWX_RFH_Q0_FRBDCB_WIDX_TRG, 8);
}

static int iwx_service(struct iwx_sc *sc)
{
    uint32_t r1 = iwx_read(sc, IWX_CSR_INT);
    if (r1 == 0xffffffff || (r1 & 0xfffffff0) == 0xa5a5a5a0) {
        if (!sc->dead_logged) {
            klogf(KLOG_ERROR, "iwx: device not responding, CSR_INT=0x%08x", r1);
            sc->dead_logged = 1;
        }
        sc->fatal = 1;
        return -1;
    }
    uint32_t r2 = iwx_read(sc, IWX_CSR_FH_INT_STATUS);
    if (r1 || r2) {
        iwx_write(sc, IWX_CSR_INT, r1);
        if ((r1 & ~sc->int_seen) || (r2 & ~sc->fh_seen))
            klogf(KLOG_INFO, "iwx: int CSR_INT=0x%08x FH_INT=0x%08x", r1, r2);
        sc->int_seen |= r1;
        sc->fh_seen |= r2;
        if (r1 & IWX_CSR_INT_BIT_ALIVE) {
            klogf(KLOG_OK, "iwx: ALIVE hw cause, posting %d rx buffers, WIDX=8",
                  IWX_RX_MQ_RING_COUNT);
            iwx_post_rx_after_alive(sc);
            sc->alive_hw = 1;
        }
        if (r1 & IWX_CSR_INT_BIT_RF_KILL)
            klogf(KLOG_WARN, "iwx: rfkill change, rfkill=%d", iwx_check_rfkill(sc));
        if (r1 & IWX_CSR_INT_BIT_SW_ERR) {
            klogf(KLOG_ERROR, "iwx: fatal firmware error (SW_ERR)");
            iwx_notif_intr(sc);
            iwx_nic_error(sc);
            iwx_dump_state(sc, "sw_err");
            sc->fatal = 1;
            return -1;
        }
        if (r1 & IWX_CSR_INT_BIT_HW_ERR) {
            klogf(KLOG_ERROR, "iwx: hardware error, stopping device");
            iwx_dump_state(sc, "hw_err");
            sc->fatal = 1;
            return -1;
        }
        if (r1 & IWX_CSR_INT_BIT_FH_TX)
            iwx_write(sc, IWX_CSR_FH_INT_STATUS, IWX_CSR_FH_INT_TX_MASK);
        if (r1 & (IWX_CSR_INT_BIT_FH_RX | IWX_CSR_INT_BIT_SW_RX))
            iwx_write(sc, IWX_CSR_FH_INT_STATUS, IWX_CSR_FH_INT_RX_MASK);
    }
    iwx_notif_intr(sc);
    return sc->fatal ? -1 : 0;
}

typedef int (*iwx_cond_fn)(struct iwx_sc *sc);

static int iwx_wait(struct iwx_sc *sc, iwx_cond_fn done, uint32_t ms, uint64_t *elapsed)
{
    uint64_t start = timer_ticks();
    for (;;) {
        if (iwx_service(sc) < 0)
            return IWX_EIO;
        if (done(sc)) {
            if (elapsed)
                *elapsed = timer_ticks() - start;
            return 0;
        }
        if (timer_ticks() - start >= ms) {
            if (elapsed)
                *elapsed = timer_ticks() - start;
            return IWX_ETIMEDOUT;
        }
        DELAY(20);
        scheduler_yield();
    }
}

static int iwx_cond_alive(struct iwx_sc *sc) { return sc->uc_intr; }
static int iwx_cond_pnvm(struct iwx_sc *sc) { return (sc->init_complete & IWX_PNVM_COMPLETE) != 0; }
static int iwx_cond_init(struct iwx_sc *sc) { return (sc->init_complete & IWX_INIT_COMPLETE) != 0; }
static int iwx_cond_cmd(struct iwx_sc *sc) { return sc->tx_done[sc->wait_idx]; }

static int iwx_send_cmd(struct iwx_sc *sc, uint32_t id, const void *data, uint16_t paylen,
                        void *resp, uint32_t resp_cap, uint32_t *resp_len)
{
    int idx = sc->tx_cur;
    uint32_t code = id;
    struct iwx_tfh_tfd *desc = (struct iwx_tfh_tfd *)sc->tx_desc.va + idx;
    struct iwx_device_cmd *cmd = (struct iwx_device_cmd *)sc->tx_cmd.va + idx;
    uint64_t paddr = sc->tx_cmd.pa + (uint64_t)idx * sizeof(struct iwx_device_cmd);
    size_t hdrlen = sizeof(cmd->hdr_wide);
    size_t datasz = sizeof(cmd->data_wide);
    uint64_t addr, elapsed = 0;

    if (paylen > datasz) {
        if (paylen > IWX_MAX_CMD_PAYLOAD_SIZE || sc->tx_big.va == NULL) {
            klogf(KLOG_ERROR, "iwx: command 0x%x too long (%u)", id, paylen);
            return IWX_EINVAL;
        }
        cmd = sc->tx_big.va;
        paddr = sc->tx_big.pa;
    }
    sc->resp_buf[idx] = resp;
    sc->resp_cap[idx] = resp_cap;
    sc->resp_got[idx] = 0;
    sc->tx_done[idx] = 0;
    if (resp)
        memset(resp, 0, resp_cap);

    if (iwx_cmd_groupid(code) == 0) {
        code = IWX_WIDE_ID(IWX_LONG_GROUP, code);
        sc->tx_flags[idx] |= IWX_TXDATA_FLAG_CMD_IS_NARROW;
    } else {
        sc->tx_flags[idx] &= (uint8_t)~IWX_TXDATA_FLAG_CMD_IS_NARROW;
    }

    memset(cmd, 0, paylen > datasz ? hdrlen + paylen : sizeof(*cmd));
    cmd->hdr_wide.opcode = iwx_cmd_opcode(code);
    cmd->hdr_wide.group_id = iwx_cmd_groupid(code);
    cmd->hdr_wide.qid = (uint8_t)sc->tx_qid;
    cmd->hdr_wide.idx = (uint8_t)idx;
    cmd->hdr_wide.length = htole16(paylen);
    cmd->hdr_wide.version = iwx_cmd_version(code);
    if (paylen)
        memcpy(cmd->data_wide, data, paylen);

    memset(desc, 0, sizeof(*desc));
    desc->tbs[0].tb_len = htole16((uint16_t)MIN(hdrlen + paylen, IWX_FIRST_TB_SIZE));
    addr = htole64(paddr);
    memcpy(&desc->tbs[0].addr, &addr, sizeof(addr));
    if (hdrlen + paylen > IWX_FIRST_TB_SIZE) {
        desc->tbs[1].tb_len = htole16((uint16_t)(hdrlen + paylen - IWX_FIRST_TB_SIZE));
        addr = htole64(paddr + IWX_FIRST_TB_SIZE);
        memcpy(&desc->tbs[1].addr, &addr, sizeof(addr));
        desc->num_tbs = htole16(2);
    } else {
        desc->num_tbs = htole16(1);
    }

    iwx_mb();
    sc->tx_queued++;
    sc->tx_cur = (sc->tx_cur + 1) % IWX_TX_RING_COUNT;
    sc->tx_cur_hw = (int)((uint32_t)(sc->tx_cur_hw + 1) % sc->max_tfd_queue_size);
    klogf(KLOG_INFO, "iwx: send cmd 0x%x (grp 0x%x op 0x%x ver %u) len %u idx %d wrptr 0x%x",
          code, cmd->hdr_wide.group_id, cmd->hdr_wide.opcode, cmd->hdr_wide.version, paylen,
          idx, (uint32_t)(sc->tx_qid << 16 | sc->tx_cur_hw));
    iwx_write(sc, IWX_HBUS_TARG_WRPTR, (uint32_t)(sc->tx_qid << 16 | sc->tx_cur_hw));

    sc->wait_idx = idx;
    int err = iwx_wait(sc, iwx_cond_cmd, 1000, &elapsed);
    if (err) {
        klogf(KLOG_ERROR, "iwx: cmd 0x%x idx %d failed: %s after %llu ms", code, idx,
              err == IWX_ETIMEDOUT ? "timeout" : "device error", (unsigned long long)elapsed);
        if (err == IWX_ETIMEDOUT)
            iwx_dump_state(sc, "cmd timeout");
        sc->resp_buf[idx] = NULL;
        return err;
    }
    klogf(KLOG_OK, "iwx: cmd 0x%x done in %llu ms", code, (unsigned long long)elapsed);
    if (resp) {
        if (resp_len)
            *resp_len = sc->resp_got[idx];
        if (!sc->resp_got[idx]) {
            sc->resp_buf[idx] = NULL;
            return IWX_EIO;
        }
    }
    sc->resp_buf[idx] = NULL;
    return 0;
}

static int iwx_nic_init(struct iwx_sc *sc)
{
    int err = iwx_apm_init(sc);
    if (err)
        klogf(KLOG_WARN, "iwx: nic_init: apm_init returned %d (ignored like iwx)", err);
    iwx_write_1(sc, IWX_CSR_INT_COALESCING, IWX_HOST_INT_TIMEOUT_DEF);
    iwx_setbits(sc, IWX_CSR_MAC_SHADOW_REG_CTRL, 0x800fffff);
    return 0;
}

static int iwx_load_firmware(struct iwx_sc *sc)
{
    uint64_t elapsed = 0;
    int err;

    sc->uc_intr = 0;
    sc->uc_ok = 0;
    sc->alive_hw = 0;

    err = iwx_ctxt_info_gen3_init(sc);
    if (err) {
        klogf(KLOG_ERROR, "iwx: could not init context info (%d)", err);
        return err;
    }
    err = iwx_wait(sc, iwx_cond_alive, 1000, &elapsed);
    if (err == IWX_ETIMEDOUT && !sc->alive_hw) {
        klogf(KLOG_WARN, "iwx: no ALIVE hw cause after %llu ms, forcing rx post and waiting 1 s more",
              (unsigned long long)elapsed);
        iwx_dump_state(sc, "alive cause timeout");
        iwx_post_rx_after_alive(sc);
        err = iwx_wait(sc, iwx_cond_alive, 1000, &elapsed);
    }
    if (err || !sc->uc_ok) {
        klogf(KLOG_ERROR, "iwx: could not load firmware, err=%d alive_hw=%d uc_intr=%d uc_ok=%d (%llu ms)",
              err, sc->alive_hw, sc->uc_intr, sc->uc_ok, (unsigned long long)elapsed);
        iwx_dump_state(sc, "alive");
        if (sc->uc_intr && (sc->uc_lmac_err[0] || sc->uc_umac_err))
            iwx_nic_error(sc);
        return IWX_EINVAL;
    }
    klogf(KLOG_OK, "iwx: firmware alive after %llu ms", (unsigned long long)elapsed);
    return 0;
}

static int iwx_start_fw(struct iwx_sc *sc)
{
    iwx_write(sc, IWX_CSR_INT, ~0u);
    iwx_disable_interrupts(sc);
    iwx_write(sc, IWX_CSR_UCODE_DRV_GP1_CLR, IWX_CSR_UCODE_SW_BIT_RFKILL);
    iwx_write(sc, IWX_CSR_UCODE_DRV_GP1_CLR, IWX_CSR_UCODE_DRV_GP1_BIT_CMD_BLOCKED);
    iwx_write(sc, IWX_CSR_INT, ~0u);
    int err = iwx_nic_init(sc);
    if (err) {
        klogf(KLOG_ERROR, "iwx: unable to init nic");
        return err;
    }
    sc->intmask = IWX_CSR_INT_BIT_ALIVE | IWX_CSR_INT_BIT_FH_RX;
    iwx_write(sc, IWX_CSR_INT_MASK, 0);
    klogf(KLOG_INFO, "iwx: fwload: iwx intmask would be 0x%08x, polling with CSR_INT_MASK=0 GP_CNTRL=0x%08x",
          sc->intmask, iwx_read(sc, IWX_CSR_GP_CNTRL));
    return iwx_load_firmware(sc);
}

static int iwx_load_pnvm(struct iwx_sc *sc)
{
    uint64_t elapsed = 0;
    if (sc->sku_id[0] == 0 && sc->sku_id[1] == 0 && sc->sku_id[2] == 0) {
        klogf(KLOG_INFO, "iwx: sku_id is zero, skipping PNVM step");
        return 0;
    }
    if (!iwx_nic_lock(sc))
        return IWX_EBUSY;
    iwx_write_umac_prph(sc, IWX_UREG_DOORBELL_TO_ISR6, IWX_UREG_DOORBELL_TO_ISR6_PNVM);
    klogf(KLOG_INFO, "iwx: no PNVM file, doorbell ISR6 <- PNVM (0x%x)", IWX_UREG_DOORBELL_TO_ISR6_PNVM);
    int err = iwx_wait(sc, iwx_cond_pnvm, 2000, &elapsed);
    iwx_nic_unlock(sc);
    if (err) {
        klogf(KLOG_ERROR, "iwx: PNVM_INIT_COMPLETE wait failed (%d) after %llu ms", err,
              (unsigned long long)elapsed);
        iwx_dump_state(sc, "pnvm");
        return err;
    }
    klogf(KLOG_OK, "iwx: PNVM step done in %llu ms", (unsigned long long)elapsed);
    return 0;
}

static int iwx_is_valid_mac_addr(const uint8_t *a)
{
    static const uint8_t reserved[6] = {0x02, 0xcc, 0xaa, 0xff, 0xee, 0x00};
    static const uint8_t bcast[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    static const uint8_t any[6] = {0, 0, 0, 0, 0, 0};
    return memcmp(reserved, a, 6) != 0 && memcmp(bcast, a, 6) != 0 &&
           memcmp(any, a, 6) != 0 && !(a[0] & 1);
}

static void iwx_flip_hw_address(uint32_t mac_addr0, uint32_t mac_addr1, uint8_t *dest)
{
    const uint8_t *hw = (const uint8_t *)&mac_addr0;
    dest[0] = hw[3];
    dest[1] = hw[2];
    dest[2] = hw[1];
    dest[3] = hw[0];
    hw = (const uint8_t *)&mac_addr1;
    dest[4] = hw[1];
    dest[5] = hw[0];
}

static void iwx_set_mac_addr_from_csr(struct iwx_sc *sc)
{
    memset(sc->mac, 0, sizeof(sc->mac));
    if (!iwx_nic_lock(sc))
        return;
    uint32_t a0 = htole32(iwx_read(sc, IWX_CSR_MAC_ADDR0_STRAP(sc)));
    uint32_t a1 = htole32(iwx_read(sc, IWX_CSR_MAC_ADDR1_STRAP(sc)));
    klogf(KLOG_INFO, "iwx: mac strap 0x%08x 0x%08x", a0, a1);
    iwx_flip_hw_address(a0, a1, sc->mac);
    if (!iwx_is_valid_mac_addr(sc->mac)) {
        a0 = htole32(iwx_read(sc, IWX_CSR_MAC_ADDR0_OTP(sc)));
        a1 = htole32(iwx_read(sc, IWX_CSR_MAC_ADDR1_OTP(sc)));
        klogf(KLOG_INFO, "iwx: mac otp 0x%08x 0x%08x", a0, a1);
        iwx_flip_hw_address(a0, a1, sc->mac);
    }
    iwx_nic_unlock(sc);
}

static void iwx_init_channel_map(struct iwx_sc *sc, const void *profile, uint32_t nprofile)
{
    int n2 = 0, n5 = 0, npassive = 0;
    sc->nchan = 0;
    for (uint32_t i = 0; i < nprofile && i < IWX_NCHAN; i++) {
        uint32_t f;
        memcpy(&f, (const uint8_t *)profile + i * 4, 4);
        f = le32toh(f);
        int is5 = i >= IWX_NUM_2GHZ_CHANNELS;
        if (is5 && !sc->nvm_5ghz)
            f &= ~IWX_NVM_CHANNEL_VALID;
        if (!(f & IWX_NVM_CHANNEL_VALID))
            continue;
        sc->chan_num[sc->nchan] = iwx_nvm_channels_8000[i];
        sc->chan_band[sc->nchan] = is5 ? IWX_PHY_BAND_5 : IWX_PHY_BAND_24;
        sc->chan_passive[sc->nchan] = !(f & IWX_NVM_CHANNEL_ACTIVE);
        npassive += sc->chan_passive[sc->nchan];
        if (is5)
            n5++;
        else
            n2++;
        sc->nchan++;
    }
    klogf(KLOG_INFO, "iwx: channel map: %d channels (2G %d, 5G %d, passive-only %d)",
          sc->nchan, n2, n5, npassive);
}

static void iwx_nvm_get(struct iwx_sc *sc)
{
    struct iwx_nvm_get_info cmd = {0};
    int v4 = isset(sc->ucode_api, IWX_UCODE_TLV_API_REGULATORY_NVM_INFO) ? 1 : 0;
    uint32_t resp_size = v4 ? sizeof(struct iwx_nvm_get_info_rsp) : sizeof(struct iwx_nvm_get_info_rsp_v3);
    uint32_t got = 0;

    iwx_set_mac_addr_from_csr(sc);
    klogf(iwx_is_valid_mac_addr(sc->mac) ? KLOG_OK : KLOG_WARN,
          "iwx: MAC address %02x:%02x:%02x:%02x:%02x:%02x%s", sc->mac[0], sc->mac[1], sc->mac[2],
          sc->mac[3], sc->mac[4], sc->mac[5], iwx_is_valid_mac_addr(sc->mac) ? "" : " (invalid)");

    int err = iwx_send_cmd(sc, IWX_WIDE_ID(IWX_REGULATORY_AND_NVM_GROUP, IWX_NVM_GET_INFO), &cmd,
                           sizeof(cmd), sc->resp_storage, sizeof(sc->resp_storage), &got);
    if (err) {
        klogf(KLOG_WARN, "iwx: NVM_GET_INFO failed (%d)", err);
        return;
    }
    const struct iwx_rx_packet *pkt = (const void *)sc->resp_storage;
    uint32_t plen = iwx_rx_packet_payload_len(pkt);
    if (plen != resp_size)
        klogf(KLOG_WARN, "iwx: NVM_GET_INFO payload %u, expected %u (api v4=%d)", plen, resp_size, v4);
    if (plen < sizeof(struct iwx_nvm_get_info_general) + sizeof(struct iwx_nvm_get_info_sku) +
                   sizeof(struct iwx_nvm_get_info_phy) + 4)
        return;
    const struct iwx_nvm_get_info_rsp *rsp = (const void *)pkt->data;
    uint32_t flags = le32toh(rsp->mac_sku.mac_sku_flags);
    klogf(KLOG_OK, "iwx: NVM version 0x%x n_hw_addrs %u board %u flags 0x%x",
          le16toh(rsp->general.nvm_version), rsp->general.n_hw_addrs, rsp->general.board_type,
          le32toh(rsp->general.flags));
    klogf(KLOG_INFO, "iwx: NVM sku 0x%08x (2G=%d 5G=%d n=%d ac=%d ax=%d mimo_dis=%d) tx_ant 0x%x rx_ant 0x%x lar %u",
          flags, !!(flags & IWX_NVM_MAC_SKU_FLAGS_BAND_2_4_ENABLED),
          !!(flags & IWX_NVM_MAC_SKU_FLAGS_BAND_5_2_ENABLED),
          !!(flags & IWX_NVM_MAC_SKU_FLAGS_802_11N_ENABLED),
          !!(flags & IWX_NVM_MAC_SKU_FLAGS_802_11AC_ENABLED),
          !!(flags & IWX_NVM_MAC_SKU_FLAGS_802_11AX_ENABLED),
          !!(flags & IWX_NVM_MAC_SKU_FLAGS_MIMO_DISABLED), le32toh(rsp->phy_sku.tx_chains),
          le32toh(rsp->phy_sku.rx_chains), le32toh(rsp->regulatory.lar_enabled));
    sc->nvm_tx_ant = le32toh(rsp->phy_sku.tx_chains);
    sc->nvm_rx_ant = le32toh(rsp->phy_sku.rx_chains);
    sc->nvm_5ghz = !!(flags & IWX_NVM_MAC_SKU_FLAGS_BAND_5_2_ENABLED);
    sc->nvm_lar = le32toh(rsp->regulatory.lar_enabled) &&
                  isset(sc->enabled_capa, IWX_UCODE_TLV_CAPA_LAR_SUPPORT);
    if (plen >= sizeof(struct iwx_nvm_get_info_rsp))
        iwx_init_channel_map(sc, (const uint8_t *)rsp + offsetof(struct iwx_nvm_get_info_rsp, regulatory.channel_profile), IWX_NCHAN);
}

static int iwx_run_init_mvm_ucode(struct iwx_sc *sc, int readnvm)
{
    struct iwx_nvm_access_complete_cmd nvm_complete = {0};
    struct iwx_init_extended_cfg_cmd init_cfg = {.init_flags = htole32(IWX_INIT_NVM)};
    uint64_t elapsed = 0;
    int err;

    if (sc->rfkill)
        klogf(KLOG_WARN, "iwx: radio is disabled by hardware switch (continuing, readnvm)");

    sc->init_complete = 0;
    err = iwx_start_fw(sc);
    if (err) {
        klogf(KLOG_ERROR, "iwx: failed to load init firmware");
        return err;
    }
    err = iwx_load_pnvm(sc);
    if (err)
        return err;
    klogf(KLOG_INFO, "iwx: post_alive: ICT not used (polling)");

    klogf(KLOG_INFO, "iwx: INIT_EXTENDED_CFG cmd ver %u, NVM_ACCESS_COMPLETE cmd ver %u",
          iwx_lookup_cmd_ver(sc, IWX_SYSTEM_GROUP, IWX_INIT_EXTENDED_CFG_CMD),
          iwx_lookup_cmd_ver(sc, IWX_REGULATORY_AND_NVM_GROUP, IWX_NVM_ACCESS_COMPLETE));
    err = iwx_send_cmd(sc, IWX_WIDE_ID(IWX_SYSTEM_GROUP, IWX_INIT_EXTENDED_CFG_CMD), &init_cfg,
                       sizeof(init_cfg), NULL, 0, NULL);
    if (err)
        return err;
    err = iwx_send_cmd(sc, IWX_WIDE_ID(IWX_REGULATORY_AND_NVM_GROUP, IWX_NVM_ACCESS_COMPLETE),
                       &nvm_complete, sizeof(nvm_complete), NULL, 0, NULL);
    if (err)
        return err;
    err = iwx_wait(sc, iwx_cond_init, 2000, &elapsed);
    if (err) {
        klogf(KLOG_ERROR, "iwx: INIT_COMPLETE wait failed (%d) after %llu ms", err,
              (unsigned long long)elapsed);
        iwx_dump_state(sc, "init_complete");
        return err;
    }
    klogf(KLOG_OK, "iwx: init firmware complete in %llu ms", (unsigned long long)elapsed);
    if (readnvm)
        iwx_nvm_get(sc);
    return 0;
}

static uint32_t iwx_fw_valid_tx_ant(struct iwx_sc *sc)
{
    uint32_t ant = (sc->phy_config & IWX_FW_PHY_CFG_TX_CHAIN) >> IWX_FW_PHY_CFG_TX_CHAIN_POS;
    if (sc->nvm_tx_ant)
        ant &= sc->nvm_tx_ant;
    return ant;
}

static uint32_t iwx_fw_valid_rx_ant(struct iwx_sc *sc)
{
    uint32_t ant = (sc->phy_config & IWX_FW_PHY_CFG_RX_CHAIN) >> IWX_FW_PHY_CFG_RX_CHAIN_POS;
    if (sc->nvm_rx_ant)
        ant &= sc->nvm_rx_ant;
    return ant;
}

static int iwx_cmd_pdu(struct iwx_sc *sc, const char *what, uint32_t id, const void *data,
                       uint16_t len)
{
    int err = iwx_send_cmd(sc, id, data, len, NULL, 0, NULL);
    if (err)
        klogf(KLOG_ERROR, "iwx: %s failed (%d)", what, err);
    return err;
}

static int iwx_send_update_mcc(struct iwx_sc *sc)
{
    struct iwx_mcc_update_cmd cmd;
    uint32_t got = 0;
    uint8_t ver = iwx_lookup_notif_ver(sc, IWX_LONG_GROUP, IWX_MCC_UPDATE_CMD);

    if (ver != IWX_FW_CMD_VER_UNKNOWN && ver >= 8) {
        klogf(KLOG_WARN, "iwx: MCC response version %u unsupported, skipping", ver);
        return 0;
    }
    memset(&cmd, 0, sizeof(cmd));
    cmd.mcc = htole16('Z' << 8 | 'Z');
    if (isset(sc->ucode_api, IWX_UCODE_TLV_API_WIFI_MCC_UPDATE) ||
        isset(sc->enabled_capa, IWX_UCODE_TLV_CAPA_LAR_MULTI_MCC))
        cmd.source_id = IWX_MCC_SOURCE_GET_CURRENT;
    else
        cmd.source_id = IWX_MCC_SOURCE_OLD_FW;

    int err = iwx_send_cmd(sc, IWX_MCC_UPDATE_CMD, &cmd, sizeof(cmd), sc->resp_storage,
                           sizeof(sc->resp_storage), &got);
    if (err) {
        klogf(KLOG_ERROR, "iwx: MCC_UPDATE failed (%d)", err);
        return err;
    }
    const struct iwx_rx_packet *pkt = (const void *)sc->resp_storage;
    uint32_t plen = iwx_rx_packet_payload_len(pkt);
    const void *channels;
    uint32_t n, status;
    uint16_t mcc;
    size_t fixed;
    if (isset(sc->enabled_capa, IWX_UCODE_TLV_CAPA_MCC_UPDATE_11AX_SUPPORT)) {
        const struct iwx_mcc_update_resp_v4 *r = (const void *)pkt->data;
        fixed = sizeof(*r);
        if (plen < fixed)
            goto bad;
        n = le32toh(r->n_channels);
        status = le32toh(r->status);
        mcc = le16toh(r->mcc);
        channels = (const uint8_t *)r + fixed;
    } else {
        const struct iwx_mcc_update_resp_v3 *r = (const void *)pkt->data;
        fixed = sizeof(*r);
        if (plen < fixed)
            goto bad;
        n = le32toh(r->n_channels);
        status = le32toh(r->status);
        mcc = le16toh(r->mcc);
        channels = (const uint8_t *)r + fixed;
    }
    if (plen != fixed + n * sizeof(uint32_t))
        goto bad;
    klogf(KLOG_OK, "iwx: MCC update: region '%c%c' status %u, %u channel profiles",
          (mcc >> 8) & 0xff ? (mcc >> 8) & 0xff : '?', mcc & 0xff ? mcc & 0xff : '?', status, n);
    iwx_init_channel_map(sc, channels, n);
    return 0;
bad:
    klogf(KLOG_ERROR, "iwx: MCC_UPDATE response malformed (payload %u)", plen);
    return IWX_EIO;
}

static int iwx_runtime_config(struct iwx_sc *sc)
{
    int err;

    struct iwx_tx_ant_cfg_cmd ant = {.valid = htole32(iwx_fw_valid_tx_ant(sc))};
    klogf(KLOG_INFO, "iwx: runtime: tx ant 0x%x rx ant 0x%x, scan cmd ver %u, scan cfg ver %u, n_scan_channels %u",
          iwx_fw_valid_tx_ant(sc), iwx_fw_valid_rx_ant(sc),
          iwx_lookup_cmd_ver(sc, IWX_LONG_GROUP, IWX_SCAN_REQ_UMAC),
          iwx_lookup_cmd_ver(sc, IWX_LONG_GROUP, IWX_SCAN_CFG_CMD), sc->n_scan_channels);
    if ((err = iwx_cmd_pdu(sc, "TX_ANT_CONFIGURATION", IWX_TX_ANT_CONFIGURATION_CMD, &ant,
                           sizeof(ant))))
        return err;

    struct iwx_bt_coex_cmd bt = {.mode = htole32(IWX_BT_COEX_WIFI), .enabled_modules = 0};
    if ((err = iwx_cmd_pdu(sc, "BT_CONFIG", IWX_BT_CONFIG, &bt, sizeof(bt))))
        return err;

    struct iwx_soc_configuration_cmd soc;
    memset(&soc, 0, sizeof(soc));
    uint32_t soc_flags = (IWX_SOC_FLAGS_LTR_APPLY_DELAY_2500 << 2) & IWX_SOC_FLAGS_LTR_APPLY_DELAY_MASK;
    uint8_t scan_ver = iwx_lookup_cmd_ver(sc, IWX_LONG_GROUP, IWX_SCAN_REQ_UMAC);
    if (scan_ver != IWX_FW_CMD_VER_UNKNOWN && scan_ver >= 2)
        soc_flags |= IWX_SOC_CONFIG_CMD_FLAGS_LOW_LATENCY;
    soc.flags = htole32(soc_flags);
    soc.latency = htole32(12000);
    if ((err = iwx_cmd_pdu(sc, "SOC_CONFIGURATION",
                           IWX_WIDE_ID(IWX_SYSTEM_GROUP, IWX_SOC_CONFIGURATION_CMD), &soc,
                           sizeof(soc))))
        return err;

    if (isset(sc->enabled_capa, IWX_UCODE_TLV_CAPA_DQA_SUPPORT)) {
        struct iwx_dqa_enable_cmd dqa = {.cmd_queue = htole32(IWX_DQA_CMD_QUEUE)};
        if ((err = iwx_cmd_pdu(sc, "DQA_ENABLE", IWX_WIDE_ID(IWX_DATA_PATH_GROUP, IWX_DQA_ENABLE_CMD),
                               &dqa, sizeof(dqa))))
            return err;
    }

    if (sc->ltr_enabled) {
        struct iwx_ltr_config_cmd ltr;
        memset(&ltr, 0, sizeof(ltr));
        ltr.flags = htole32(IWX_LTR_CFG_FLAG_FEATURE_ENABLE);
        if (iwx_send_cmd(sc, IWX_LTR_CONFIG, &ltr, sizeof(ltr), NULL, 0, NULL))
            klogf(KLOG_WARN, "iwx: LTR_CONFIG failed (ignored)");
    }

    if (isset(sc->enabled_capa, IWX_UCODE_TLV_CAPA_CT_KILL_BY_FW)) {
        struct iwx_temp_report_ths_cmd ths;
        memset(&ths, 0, sizeof(ths));
        if ((err = iwx_cmd_pdu(sc, "TEMP_REPORTING_THRESHOLDS",
                               IWX_WIDE_ID(IWX_PHY_OPS_GROUP, IWX_TEMP_REPORTING_THRESHOLDS_CMD),
                               &ths, sizeof(ths))))
            return err;
    }

    struct iwx_device_power_cmd pwr = {0};
    if ((err = iwx_cmd_pdu(sc, "POWER_TABLE", IWX_POWER_TABLE_CMD, &pwr, sizeof(pwr))))
        return err;

    if (sc->nvm_lar) {
        if ((err = iwx_send_update_mcc(sc)))
            return err;
    } else {
        klogf(KLOG_INFO, "iwx: LAR disabled, using NVM channel map");
    }

    if (!isset(sc->ucode_api, IWX_UCODE_TLV_API_REDUCED_SCAN_CONFIG)) {
        klogf(KLOG_ERROR, "iwx: firmware lacks REDUCED_SCAN_CONFIG api");
        return IWX_EINVAL;
    }
    struct iwx_scan_config scfg;
    memset(&scfg, 0, sizeof(scfg));
    uint8_t cfg_ver = iwx_lookup_cmd_ver(sc, IWX_LONG_GROUP, IWX_SCAN_CFG_CMD);
    scfg.bcast_sta_id = (cfg_ver == IWX_FW_CMD_VER_UNKNOWN || cfg_ver < 5) ? 0xff : 0;
    scfg.tx_chains = htole32(iwx_fw_valid_tx_ant(sc));
    scfg.rx_chains = htole32(iwx_fw_valid_rx_ant(sc));
    if ((err = iwx_cmd_pdu(sc, "SCAN_CFG", IWX_WIDE_ID(IWX_LONG_GROUP, IWX_SCAN_CFG_CMD), &scfg,
                           sizeof(scfg))))
        return err;

    struct iwx_beacon_filter_cmd bf;
    memset(&bf, 0, sizeof(bf));
    if ((err = iwx_cmd_pdu(sc, "BEACON_FILTERING", IWX_REPLY_BEACON_FILTERING_CMD, &bf,
                           sizeof(bf))))
        return err;

    klogf(KLOG_OK, "iwx: runtime configuration done");
    return 0;
}

static void iwx_fill_probe_req(struct iwx_sc *sc, struct iwx_scan_probe_req *preq)
{
    static const uint8_t rates_2g[] = {0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24};
    static const uint8_t xrates_2g[] = {0x30, 0x48, 0x60, 0x6c};
    static const uint8_t rates_5g[] = {0x8c, 0x12, 0x98, 0x24, 0xb0, 0x48, 0x60, 0x6c};
    struct ieee80211_frame *wh = (struct ieee80211_frame *)preq->buf;
    uint8_t *frm, *pos;

    memset(preq, 0, sizeof(*preq));
    wh->i_fc[0] = 0x40;
    wh->i_fc[1] = 0x00;
    memset(wh->i_addr1, 0xff, 6);
    memcpy(wh->i_addr2, sc->mac, 6);
    memset(wh->i_addr3, 0xff, 6);

    frm = (uint8_t *)(wh + 1);
    *frm++ = 0;
    *frm++ = 0;
    preq->mac_header.offset = 0;
    preq->mac_header.len = htole16((uint16_t)(frm - (uint8_t *)wh));

    preq->band_data[0].offset = htole16((uint16_t)(frm - (uint8_t *)wh));
    pos = frm;
    *frm++ = 1;
    *frm++ = sizeof(rates_2g);
    memcpy(frm, rates_2g, sizeof(rates_2g));
    frm += sizeof(rates_2g);
    *frm++ = 50;
    *frm++ = sizeof(xrates_2g);
    memcpy(frm, xrates_2g, sizeof(xrates_2g));
    frm += sizeof(xrates_2g);
    if (isset(sc->enabled_capa, IWX_UCODE_TLV_CAPA_DS_PARAM_SET_IE_SUPPORT)) {
        *frm++ = 3;
        *frm++ = 1;
        *frm++ = 0;
    }
    preq->band_data[0].len = htole16((uint16_t)(frm - pos));

    if (sc->nvm_5ghz) {
        preq->band_data[1].offset = htole16((uint16_t)(frm - (uint8_t *)wh));
        pos = frm;
        *frm++ = 1;
        *frm++ = sizeof(rates_5g);
        memcpy(frm, rates_5g, sizeof(rates_5g));
        frm += sizeof(rates_5g);
        preq->band_data[1].len = htole16((uint16_t)(frm - pos));
    }
    preq->common_data.offset = htole16((uint16_t)(frm - (uint8_t *)wh));
    preq->common_data.len = 0;
}

static int iwx_cond_scan(struct iwx_sc *sc) { return sc->scan_done >= 2; }

static int iwx_scan(struct iwx_sc *sc)
{
    static uint8_t scan_buf[IWX_PAGE_SIZE] __attribute__((aligned(16)));
    uint8_t ver = iwx_lookup_cmd_ver(sc, IWX_LONG_GROUP, IWX_SCAN_REQ_UMAC);
    uint16_t gen_flags = IWX_UMAC_SCAN_GEN_FLAGS_V2_FORCE_PASSIVE |
                         IWX_UMAC_SCAN_GEN_FLAGS_V2_PASS_ALL |
                         IWX_UMAC_SCAN_GEN_FLAGS_V2_NTFY_ITER_COMPLETE |
                         IWX_UMAC_SCAN_GEN_FLAGS_V2_ADAPTIVE_DWELL;
    uint32_t maxchan = sc->n_scan_channels ? sc->n_scan_channels : 67;
    uint16_t len;
    int n = 0;

    if (sc->nchan == 0) {
        klogf(KLOG_ERROR, "iwx: scan: no valid channels");
        return IWX_EINVAL;
    }
    memset(scan_buf, 0, sizeof(scan_buf));

    if (ver == 17) {
        struct iwx_scan_req_umac_v17 *cmd = (void *)scan_buf;
        struct iwx_scan_req_params_v17 *sp = &cmd->scan_params;
        struct iwx_scan_general_params_v11 *gp = &sp->general_params;
        _Static_assert(sizeof(*cmd) <= sizeof(scan_buf), "scan v17 too big");
        cmd->uid = 0;
        cmd->ooc_priority = htole32(IWX_SCAN_PRIORITY_EXT_6);
        gp->flags = htole16(gen_flags);
        gp->scan_start_mac_or_link_id = 0;
        gp->adwell_default_social_chn = IWX_SCAN_ADWELL_DEFAULT_N_APS_SOCIAL;
        gp->adwell_default_2g = IWX_SCAN_ADWELL_DEFAULT_LB_N_APS;
        gp->adwell_default_5g = IWX_SCAN_ADWELL_DEFAULT_HB_N_APS;
        gp->adwell_max_budget = htole16(IWX_SCAN_ADWELL_MAX_BUDGET_FULL_SCAN);
        gp->scan_priority = htole32(IWX_SCAN_PRIORITY_EXT_6);
        for (int i = 0; i < IWX_SCAN_TWO_LMACS; i++) {
            gp->active_dwell[i] = IWX_SCAN_DWELL_ACTIVE;
            gp->passive_dwell[i] = IWX_SCAN_DWELL_PASSIVE;
        }
        sp->periodic_params.schedule[0].interval = 0;
        sp->periodic_params.schedule[0].iter_count = 1;
        iwx_fill_probe_req(sc, &sp->probe_params.preq);
        sp->channel_params.flags = IWX_SCAN_CHANNEL_FLAG_ENABLE_CHAN_ORDER;
        sp->channel_params.n_aps_override[0] = IWX_SCAN_ADWELL_N_APS_GO_FRIENDLY;
        sp->channel_params.n_aps_override[1] = IWX_SCAN_ADWELL_N_APS_SOCIAL_CHS;
        for (int i = 0; i < sc->nchan && n < 67 && (uint32_t)n < maxchan; i++, n++) {
            struct iwx_scan_channel_cfg_umac_v5 *c = &sp->channel_params.channel_config[n];
            c->channel_num = sc->chan_num[i];
            c->psd_20 = (uint8_t)-128;
            c->iter_count = 1;
            c->iter_interval = 0;
            c->flags = htole32((uint32_t)sc->chan_band[i] << IWX_CHAN_CFG_FLAGS_BAND_POS);
        }
        sp->channel_params.count = (uint8_t)n;
        len = sizeof(*cmd);
    } else {
        struct iwx_scan_req_umac_v14 *cmd = (void *)scan_buf;
        struct iwx_scan_req_params_v14 *sp = &cmd->scan_params;
        struct iwx_scan_general_params_v10 *gp = &sp->general_params;
        _Static_assert(sizeof(*cmd) <= sizeof(scan_buf), "scan v14 too big");
        int ext = isset(sc->ucode_api, IWX_UCODE_TLV_API_SCAN_EXT_CHAN_VER) ? 1 : 0;
        cmd->uid = 0;
        cmd->ooc_priority = htole32(IWX_SCAN_PRIORITY_EXT_6);
        gp->flags = htole16(gen_flags);
        gp->scan_start_mac_id = 0;
        gp->adwell_default_social_chn = IWX_SCAN_ADWELL_DEFAULT_N_APS_SOCIAL;
        gp->adwell_default_2g = IWX_SCAN_ADWELL_DEFAULT_LB_N_APS;
        gp->adwell_default_5g = IWX_SCAN_ADWELL_DEFAULT_HB_N_APS;
        gp->adwell_max_budget = htole16(IWX_SCAN_ADWELL_MAX_BUDGET_FULL_SCAN);
        gp->scan_priority = htole32(IWX_SCAN_PRIORITY_EXT_6);
        for (int i = 0; i < IWX_SCAN_TWO_LMACS; i++) {
            gp->active_dwell[i] = IWX_SCAN_DWELL_ACTIVE;
            gp->passive_dwell[i] = IWX_SCAN_DWELL_PASSIVE;
        }
        sp->periodic_params.schedule[0].interval = 0;
        sp->periodic_params.schedule[0].iter_count = 1;
        iwx_fill_probe_req(sc, &sp->probe_params.preq);
        sp->channel_params.flags = IWX_SCAN_CHANNEL_FLAG_ENABLE_CHAN_ORDER;
        sp->channel_params.n_aps_override[0] = IWX_SCAN_ADWELL_N_APS_GO_FRIENDLY;
        sp->channel_params.n_aps_override[1] = IWX_SCAN_ADWELL_N_APS_SOCIAL_CHS;
        for (int i = 0; i < sc->nchan && n < 67 && (uint32_t)n < maxchan; i++, n++) {
            struct iwx_scan_channel_cfg_umac *c = &sp->channel_params.channel_config[n];
            c->flags = 0;
            if (ext) {
                c->v2.channel_num = sc->chan_num[i];
                c->v2.band = sc->chan_band[i];
                c->v2.iter_count = 1;
                c->v2.iter_interval = 0;
            } else {
                c->v1.channel_num = sc->chan_num[i];
                c->v1.iter_count = 1;
                c->v1.iter_interval = 0;
            }
        }
        sp->channel_params.count = (uint8_t)n;
        len = sizeof(*cmd);
    }

    sc->scan_done = 0;
    sc->scan_beacons = 0;
    sc->nbss = 0;
    klogf(KLOG_INFO, "iwx: starting passive scan: cmd ver %u, %d channels, request %u bytes", ver, n,
          len);
    uint64_t start = timer_ticks();
    int err = iwx_send_cmd(sc, IWX_WIDE_ID(IWX_LONG_GROUP, IWX_SCAN_REQ_UMAC), scan_buf, len, NULL,
                           0, NULL);
    if (err) {
        klogf(KLOG_ERROR, "iwx: SCAN_REQ_UMAC failed (%d)", err);
        return err;
    }
    err = iwx_wait(sc, iwx_cond_scan, 15000, NULL);
    uint64_t took = timer_ticks() - start;
    if (err && sc->scan_done == 0) {
        klogf(KLOG_ERROR, "iwx: scan did not complete (%d) after %llu ms, %u frames", err,
              (unsigned long long)took, sc->scan_beacons);
        return err ? err : IWX_ETIMEDOUT;
    }
    klogf(KLOG_OK, "iwx: scan finished in %llu ms: %u beacons/probe responses, %d networks",
          (unsigned long long)took, sc->scan_beacons, sc->nbss);
    for (int i = 0; i < sc->nbss; i++) {
        const struct iwx_bss *b = &sc->bss[i];
        int hidden = b->ssid_len == 0 || b->ssid[0] == 0;
        const char *sec = b->rsn ? "WPA2/3" : b->wpa ? "WPA" : (b->capinfo & 0x10) ? "WEP" : "open";
        klogf(KLOG_INFO, "iwx:  [%2d] ch %3u %4d dBm %-6s %02x:%02x:%02x:%02x:%02x:%02x \"%s\"%s",
              i, b->channel, b->rssi, sec, b->bssid[0], b->bssid[1], b->bssid[2], b->bssid[3],
              b->bssid[4], b->bssid[5],
              hidden ? "" : b->ssid, hidden ? " (hidden)" : "");
    }
    return 0;
}

static void iwx_disable_rx_dma(struct iwx_sc *sc)
{
    if (iwx_nic_lock(sc)) {
        iwx_write_umac_prph(sc, IWX_RFH_RXF_DMA_CFG_GEN3, 0);
        int ntries;
        for (ntries = 0; ntries < 1000; ntries++) {
            if (iwx_read_umac_prph(sc, IWX_RFH_GEN_STATUS_GEN3) & IWX_RXF_DMA_IDLE)
                break;
            DELAY(10);
        }
        if (ntries == 1000)
            klogf(KLOG_WARN, "iwx: rx dma did not go idle");
        iwx_nic_unlock(sc);
    }
}

static int iwx_send_cmd_status(struct iwx_sc *sc, const char *what, uint32_t id, const void *data,
                               uint16_t len, uint32_t *status)
{
    uint32_t got = 0;
    int err = iwx_send_cmd(sc, id, data, len, sc->resp_storage, sizeof(sc->resp_storage), &got);
    if (err) {
        klogf(KLOG_ERROR, "iwx: %s failed (%d)", what, err);
        return err;
    }
    const struct iwx_rx_packet *pkt = (const void *)sc->resp_storage;
    if (iwx_rx_packet_payload_len(pkt) < 4) {
        klogf(KLOG_ERROR, "iwx: %s: short status response", what);
        return IWX_EIO;
    }
    memcpy(status, pkt->data, 4);
    *status = le32toh(*status);
    return 0;
}

static uint32_t iwx_rxchain_info(struct iwx_sc *sc)
{
    return (iwx_fw_valid_rx_ant(sc) << IWX_PHY_RX_CHAIN_VALID_POS) |
           (1u << IWX_PHY_RX_CHAIN_CNT_POS) | (1u << IWX_PHY_RX_CHAIN_MIMO_CNT_POS);
}

static int iwx_phy_ctxt(struct iwx_sc *sc, uint32_t action)
{
    uint8_t ver = iwx_lookup_cmd_ver(sc, IWX_LONG_GROUP, IWX_PHY_CONTEXT_CMD);
    uint8_t rlc_ver = iwx_lookup_cmd_ver(sc, IWX_DATA_PATH_GROUP, IWX_RLC_CONFIG_CMD);
    int is2g = sc->ap->channel <= 14;
    uint8_t band = is2g ? IWX_PHY_BAND_24 : IWX_PHY_BAND_5;
    uint32_t rxchain = rlc_ver == 2 ? 0 : iwx_rxchain_info(sc);
    int err;

    if (ver != 3 && ver != 4) {
        klogf(KLOG_ERROR, "iwx: PHY_CONTEXT_CMD version %u unsupported", ver);
        return IWX_EINVAL;
    }
    if (isset(sc->enabled_capa, IWX_UCODE_TLV_CAPA_ULTRA_HB_CHANNELS)) {
        struct iwx_phy_context_cmd_uhb cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.id_and_color = htole32(IWX_FW_CMD_ID_AND_COLOR(0, 0));
        cmd.action = htole32(action);
        cmd.ci.channel = htole32(sc->ap->channel);
        cmd.ci.band = band;
        cmd.ci.width = IWX_PHY_VHT_CHANNEL_MODE20;
        cmd.ci.ctrl_pos = IWX_PHY_VHT_CTRL_POS_1_BELOW;
        cmd.lmac_id = htole32(sc->lmac_id);
        cmd.rxchain_info = htole32(rxchain);
        err = iwx_cmd_pdu(sc, "PHY_CONTEXT(uhb)", IWX_PHY_CONTEXT_CMD, &cmd, sizeof(cmd));
    } else {
        struct iwx_phy_context_cmd cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.id_and_color = htole32(IWX_FW_CMD_ID_AND_COLOR(0, 0));
        cmd.action = htole32(action);
        cmd.ci.channel = sc->ap->channel;
        cmd.ci.band = band;
        cmd.ci.width = IWX_PHY_VHT_CHANNEL_MODE20;
        cmd.ci.ctrl_pos = IWX_PHY_VHT_CTRL_POS_1_BELOW;
        cmd.lmac_id = htole32(sc->lmac_id);
        cmd.rxchain_info = htole32(rxchain);
        err = iwx_cmd_pdu(sc, "PHY_CONTEXT", IWX_PHY_CONTEXT_CMD, &cmd, sizeof(cmd));
    }
    if (err || action == IWX_FW_CTXT_ACTION_REMOVE || rlc_ver != 2)
        return err;

    struct iwx_rlc_config_cmd rlc;
    memset(&rlc, 0, sizeof(rlc));
    rlc.phy_id = htole32(0);
    rlc.rlc.rx_chain_info = htole32(iwx_rxchain_info(sc));
    return iwx_cmd_pdu(sc, "RLC_CONFIG", IWX_WIDE_ID(IWX_DATA_PATH_GROUP, IWX_RLC_CONFIG_CMD) | (2u << 16),
                       &rlc, sizeof(rlc));
}

static int iwx_mac_ctxt(struct iwx_sc *sc, uint32_t action, int assoc)
{
    static const struct {
        uint8_t ecwmin, ecwmax, aifsn;
        uint16_t txop;
    } edca[4] = {{4, 10, 3, 0}, {4, 10, 7, 0}, {3, 4, 2, 94}, {2, 3, 2, 47}};
    static const uint8_t ac_fifo[4] = {IWX_GEN2_EDCA_TX_FIFO_BE, IWX_GEN2_EDCA_TX_FIFO_BK,
                                       IWX_GEN2_EDCA_TX_FIFO_VI, IWX_GEN2_EDCA_TX_FIFO_VO};
    struct iwx_mac_ctx_cmd cmd;
    const struct iwx_bss *ap = sc->ap;

    memset(&cmd, 0, sizeof(cmd));
    cmd.id_and_color = htole32(IWX_FW_CMD_ID_AND_COLOR(0, 0));
    cmd.action = htole32(action);
    if (action == IWX_FW_CTXT_ACTION_REMOVE)
        return iwx_cmd_pdu(sc, "MAC_CONTEXT remove", IWX_MAC_CONTEXT_CMD, &cmd, sizeof(cmd));

    cmd.mac_type = htole32(IWX_FW_MAC_TYPE_BSS_STA);
    cmd.tsf_id = htole32(IWX_TSF_ID_A);
    memcpy(cmd.node_addr, sc->mac, 6);
    memcpy(cmd.bssid_addr, ap->bssid, 6);
    cmd.cck_rates = htole32(ap->channel <= 14 ? 0x0f : 0);
    cmd.ofdm_rates = htole32(0x15);
    cmd.cck_short_preamble = htole32((ap->capinfo & 0x0020) ? IWX_MAC_FLG_SHORT_PREAMBLE : 0);
    cmd.short_slot = htole32((ap->capinfo & 0x0400) ? IWX_MAC_FLG_SHORT_SLOT : 0);
    for (int i = 0; i < 4; i++) {
        int txf = ac_fifo[i];
        cmd.ac[txf].cw_min = htole16(IWX_EXP2(edca[i].ecwmin));
        cmd.ac[txf].cw_max = htole16(IWX_EXP2(edca[i].ecwmax));
        cmd.ac[txf].aifsn = edca[i].aifsn;
        cmd.ac[txf].fifos_mask = (uint8_t)(1 << txf);
        cmd.ac[txf].edca_txop = htole16(edca[i].txop * 32);
    }
    uint32_t filter = IWX_MAC_FILTER_ACCEPT_GRP;
    uint16_t bi = ap->bintval ? ap->bintval : 100;
    uint8_t dtim_period = ap->dtim_period ? ap->dtim_period : 1;
    if (!assoc || !sc->mlme.aid)
        filter |= IWX_MAC_FILTER_IN_BEACON;
    cmd.filter_flags = htole32(filter);
    cmd.sta.bi = htole32(bi);
    cmd.sta.dtim_interval = htole32((uint32_t)bi * dtim_period);
    cmd.sta.listen_interval = htole32(10);
    if (assoc) {
        uint32_t dtim_off = (uint32_t)ap->dtim_count * bi * 1024;
        cmd.sta.is_assoc = htole32(1);
        cmd.sta.dtim_time = htole32(ap->gp2 + dtim_off);
        cmd.sta.dtim_tsf = htole64(ap->tsf + dtim_off);
        cmd.sta.assoc_beacon_arrive_time = htole32(ap->gp2);
        cmd.sta.assoc_id = htole32(sc->mlme.aid & 0x3fff);
    }
    return iwx_cmd_pdu(sc, assoc ? "MAC_CONTEXT assoc" : "MAC_CONTEXT", IWX_MAC_CONTEXT_CMD, &cmd,
                       sizeof(cmd));
}

static int iwx_binding(struct iwx_sc *sc, uint32_t action)
{
    struct iwx_binding_cmd cmd;
    uint32_t status = 0;
    memset(&cmd, 0, sizeof(cmd));
    cmd.id_and_color = htole32(IWX_FW_CMD_ID_AND_COLOR(0, 0));
    cmd.action = htole32(action);
    cmd.phy = htole32(IWX_FW_CMD_ID_AND_COLOR(0, 0));
    cmd.macs[0] = htole32(action == IWX_FW_CTXT_ACTION_REMOVE ? IWX_FW_CTXT_INVALID
                                                              : IWX_FW_CMD_ID_AND_COLOR(0, 0));
    for (int i = 1; i < IWX_MAX_MACS_IN_BINDING; i++)
        cmd.macs[i] = htole32(IWX_FW_CTXT_INVALID);
    cmd.lmac_id = htole32(sc->lmac_id);
    int err = iwx_send_cmd_status(sc, "BINDING", IWX_BINDING_CONTEXT_CMD, &cmd, sizeof(cmd), &status);
    if (!err && status) {
        klogf(KLOG_ERROR, "iwx: BINDING status 0x%x", status);
        err = IWX_EIO;
    }
    return err;
}

static int iwx_add_sta(struct iwx_sc *sc, int update)
{
    struct iwx_add_sta_cmd cmd;
    uint32_t status = 0;
    memset(&cmd, 0, sizeof(cmd));
    cmd.add_modify = update ? 1 : 0;
    cmd.mac_id_n_color = htole32(IWX_FW_CMD_ID_AND_COLOR(0, 0));
    if (!update)
        memcpy(cmd.addr, sc->ap->bssid, 6);
    cmd.sta_id = IWX_STATION_ID;
    cmd.station_type = IWX_STA_LINK;
    cmd.station_flags_msk = htole32(IWX_STA_FLG_FAT_EN_MSK | IWX_STA_FLG_MIMO_EN_MSK);
    if (update)
        cmd.assoc_id = htole16(sc->mlme.aid & 0x3fff);
    int err = iwx_send_cmd_status(sc, update ? "ADD_STA modify" : "ADD_STA", IWX_ADD_STA, &cmd,
                                  sizeof(cmd), &status);
    if (!err && (status & IWX_ADD_STA_STATUS_MASK) != IWX_ADD_STA_SUCCESS) {
        klogf(KLOG_ERROR, "iwx: ADD_STA status 0x%x", status);
        err = IWX_EIO;
    }
    return err;
}

static int iwx_rm_sta(struct iwx_sc *sc)
{
    struct iwx_rm_sta_cmd cmd = {.sta_id = IWX_STATION_ID};
    return iwx_cmd_pdu(sc, "REMOVE_STA", IWX_REMOVE_STA, &cmd, sizeof(cmd));
}

static int iwx_enable_mgmt_queue(struct iwx_sc *sc)
{
    uint8_t ver = iwx_lookup_cmd_ver(sc, IWX_DATA_PATH_GROUP, IWX_SCD_QUEUE_CONFIG_CMD);
    struct iwx_scd_queue_cfg_cmd cmd;
    uint32_t got = 0;

    if (ver != 3) {
        klogf(KLOG_ERROR, "iwx: SCD_QUEUE_CONFIG version %u unsupported", ver);
        return IWX_EINVAL;
    }
    memset(sc->mq_desc.va, 0, sc->mq_desc.size);
    memset(sc->mq_bc.va, 0, sc->mq_bc.size);
    sc->mq_cur = 0;
    sc->mq_cur_hw = 0;
    sc->mq_tail_hw = 0;
    memset(&cmd, 0, sizeof(cmd));
    cmd.operation = htole32(IWX_SCD_QUEUE_ADD);
    cmd.u.add.sta_mask = htole32(1u << IWX_STATION_ID);
    cmd.u.add.tid = IWX_MGMT_TID;
    cmd.u.add.cb_size = htole32(IWX_TFD_QUEUE_CB_SIZE(IWX_TX_RING_COUNT));
    cmd.u.add.bc_dram_addr = htole64(sc->mq_bc.pa);
    cmd.u.add.tfdq_dram_addr = htole64(sc->mq_desc.pa);
    int err = iwx_send_cmd(sc, IWX_WIDE_ID(IWX_DATA_PATH_GROUP, IWX_SCD_QUEUE_CONFIG_CMD) | (3u << 16),
                           &cmd, sizeof(cmd), sc->resp_storage, sizeof(sc->resp_storage), &got);
    if (err) {
        klogf(KLOG_ERROR, "iwx: SCD_QUEUE_CONFIG failed (%d)", err);
        return err;
    }
    const struct iwx_rx_packet *pkt = (const void *)sc->resp_storage;
    const struct iwx_tx_queue_cfg_rsp *r = (const void *)pkt->data;
    uint32_t plen = iwx_rx_packet_payload_len(pkt);
    if (plen < sizeof(*r)) {
        klogf(KLOG_ERROR, "iwx: SCD_QUEUE_CONFIG response %u bytes", plen);
        return IWX_EIO;
    }
    klogf(KLOG_INFO, "iwx: mgmt queue: fw queue %u flags 0x%x wptr %u (payload %u)",
          le16toh(r->queue_number), le16toh(r->flags), le16toh(r->write_pointer), plen);
    if (le16toh(r->queue_number) != IWX_MQ_ID) {
        klogf(KLOG_ERROR, "iwx: firmware assigned queue %u, expected %u", le16toh(r->queue_number),
              IWX_MQ_ID);
        return IWX_EIO;
    }
    sc->mq_cur_hw = le16toh(r->write_pointer);
    sc->mq_tail_hw = sc->mq_cur_hw;
    sc->mq_enabled = 1;
    return 0;
}

static int iwx_disable_mgmt_queue(struct iwx_sc *sc)
{
    struct iwx_scd_queue_cfg_cmd cmd;
    uint32_t got = 0;
    memset(&cmd, 0, sizeof(cmd));
    cmd.operation = htole32(IWX_SCD_QUEUE_REMOVE);
    cmd.u.remove.sta_mask = htole32(1u << IWX_STATION_ID);
    cmd.u.remove.tid = htole32(IWX_MGMT_TID);
    int err = iwx_send_cmd(sc, IWX_WIDE_ID(IWX_DATA_PATH_GROUP, IWX_SCD_QUEUE_CONFIG_CMD) | (3u << 16),
                           &cmd, sizeof(cmd), sc->resp_storage, sizeof(sc->resp_storage), &got);
    if (err)
        klogf(KLOG_ERROR, "iwx: could not remove mgmt queue (%d)", err);
    else
        klogf(KLOG_INFO, "iwx: mgmt queue removed");
    sc->mq_enabled = 0;
    return err;
}

static int iwx_session_protect(struct iwx_sc *sc, uint32_t action, uint32_t duration_tu)
{
    struct iwx_session_prot_cmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.id_and_color = htole32(IWX_FW_CMD_ID_AND_COLOR(0, 0));
    cmd.action = htole32(action);
    cmd.conf_id = htole32(IWX_SESSION_PROTECT_CONF_ASSOC);
    cmd.duration_tu = htole32(duration_tu);
    return iwx_cmd_pdu(sc, "SESSION_PROTECTION",
                       IWX_WIDE_ID(IWX_MAC_CONF_GROUP, IWX_SESSION_PROTECTION_CMD), &cmd, sizeof(cmd));
}

static uint32_t iwx_mgmt_rate(struct iwx_sc *sc)
{
    uint32_t r = IWX_RATE_MCS_ANT_A_MSK;
    if (sc->ap->channel <= 14) {
        if (sc->rate_v2)
            r |= IWX_RATE_MCS_CCK_MSK | 0;
        else
            r |= IWX_RATE_MCS_CCK_MSK_V1 | IWX_RATE_1M_PLCP;
    } else {
        if (sc->rate_v2)
            r |= IWX_RATE_MCS_LEGACY_OFDM_MSK | 0;
        else
            r |= IWX_RATE_6M_PLCP;
    }
    return r;
}

static uint32_t iwx_data_rate(struct iwx_sc *sc)
{
    uint32_t r = IWX_RATE_MCS_ANT_A_MSK;
    if (sc->ap->channel <= 14)
        r |= sc->rate_v2 ? (IWX_RATE_MCS_CCK_MSK | 3) : (IWX_RATE_MCS_CCK_MSK_V1 | IWX_RATE_11M_PLCP);
    else
        r |= sc->rate_v2 ? (IWX_RATE_MCS_LEGACY_OFDM_MSK | 4) : IWX_RATE_24M_PLCP;
    return r;
}

static bool iwx_tx_frame(struct iwx_sc *sc, const uint8_t *frame, uint16_t len, int eapol)
{
    const uint32_t hdrlen = 24;
    if (!sc->mq_enabled || !sc->ap || len < hdrlen || len - hdrlen > IWX_MQ_BUF)
        return false;
    uint32_t inflight = (uint32_t)(sc->mq_cur_hw - sc->mq_tail_hw) & 0xffff;
    if (inflight >= IWX_TX_RING_COUNT - 8)
        return false;
    bool is_data = (frame[0] & 0x0c) == 0x08;
    uint32_t rate = is_data ? iwx_data_rate(sc) : iwx_mgmt_rate(sc);
    int idx = sc->mq_cur;
    struct iwx_device_cmd *cmd = (struct iwx_device_cmd *)sc->mq_cmd.va + idx;
    uint64_t cmd_pa = sc->mq_cmd.pa + (uint64_t)idx * sizeof(struct iwx_device_cmd);
    uint8_t *buf = (uint8_t *)sc->mq_buf.va + (size_t)idx * IWX_MQ_BUF;
    uint64_t buf_pa = sc->mq_buf.pa + (uint64_t)idx * IWX_MQ_BUF;
    struct iwx_tfh_tfd *desc = (struct iwx_tfh_tfd *)sc->mq_desc.va + idx;
    uint16_t body = (uint16_t)(len - hdrlen);
    uint16_t flags = IWX_TX_FLAGS_CMD_RATE | IWX_TX_FLAGS_ENCRYPT_DIS;
    if (eapol)
        flags |= IWX_TX_FLAGS_HIGH_PRI;

    memset(cmd, 0, sizeof(*cmd));
    cmd->hdr.code = IWX_TX_CMD;
    cmd->hdr.flags = 0;
    cmd->hdr.qid = IWX_MQ_ID;
    cmd->hdr.idx = (uint8_t)idx;
    struct iwx_tx_cmd_gen3 *tx = (void *)cmd->data;
    tx->len = htole16(len);
    tx->flags = htole16(flags);
    tx->offload_assist = htole32(IWX_TX_CMD_OFFLD_MH_SIZE((hdrlen / 2) & IWX_TX_CMD_OFFLD_MH_MASK));
    tx->rate_n_flags = htole32(rate);
    memcpy(tx->hdr, frame, hdrlen);
    if (body)
        memcpy(buf, frame + hdrlen, body);

    uint16_t num_tbs = body ? 3 : 2;
    uint64_t addr;
    memset(desc, 0, sizeof(*desc));
    desc->num_tbs = htole16(num_tbs);
    desc->tbs[0].tb_len = htole16(IWX_FIRST_TB_SIZE);
    addr = htole64(cmd_pa);
    memcpy(&desc->tbs[0].addr, &addr, sizeof(addr));
    desc->tbs[1].tb_len = htole16((uint16_t)(sizeof(struct iwx_cmd_header) + sizeof(*tx) + hdrlen -
                                             IWX_FIRST_TB_SIZE));
    addr = htole64(cmd_pa + IWX_FIRST_TB_SIZE);
    memcpy(&desc->tbs[1].addr, &addr, sizeof(addr));
    if (body) {
        desc->tbs[2].tb_len = htole16(body);
        addr = htole64(buf_pa);
        memcpy(&desc->tbs[2].addr, &addr, sizeof(addr));
    }

    uint32_t filled = 2 + num_tbs * 10;
    uint32_t chunks = (filled + 63) / 64 - 1;
    uint16_t *bc = sc->mq_bc.va;
    bc[idx] = htole16((uint16_t)(len | (chunks << 14)));

    iwx_mb();
    sc->mq_cur = (sc->mq_cur + 1) % IWX_TX_RING_COUNT;
    sc->mq_cur_hw = (int)((uint32_t)(sc->mq_cur_hw + 1) % sc->max_tfd_queue_size);
    if (!is_data)
        klogf(KLOG_INFO, "iwx: tx q%d idx %d: fc 0x%02x%02x len %u rate 0x%08x flags 0x%x", IWX_MQ_ID, idx,
              frame[1], frame[0], len, rate, flags);
    iwx_write(sc, IWX_HBUS_TARG_WRPTR, (uint32_t)(IWX_MQ_ID << 16 | sc->mq_cur_hw));
    return true;
}

static bool iwx_mlme_tx(void *ctx, const uint8_t *frame, uint16_t len)
{
    return iwx_tx_frame(ctx, frame, len, 0);
}

static int iwx_rsn_suite_has(const uint8_t *p, uint16_t count, uint8_t type)
{
    for (uint16_t i = 0; i < count; i++)
        if (p[i * 4] == 0x00 && p[i * 4 + 1] == 0x0f && p[i * 4 + 2] == 0xac && p[i * 4 + 3] == type)
            return 1;
    return 0;
}

static int iwx_build_rsn(const struct iwx_bss *b, uint8_t *out, const char **why)
{
    const uint8_t *ie = b->rsn_ie;
    uint32_t n = b->rsn_len;
    if (n < 2 + 2 + 4 + 2) {
        *why = "RSN IE too short";
        return -1;
    }
    const uint8_t *group = ie + 4;
    uint32_t off = 8;
    uint16_t pc = (uint16_t)(ie[off] | ie[off + 1] << 8);
    off += 2;
    if (off + pc * 4u + 2 > n) {
        *why = "RSN pairwise list truncated";
        return -1;
    }
    int ccmp = iwx_rsn_suite_has(ie + off, pc, 4);
    off += pc * 4u;
    uint16_t ac = (uint16_t)(ie[off] | ie[off + 1] << 8);
    off += 2;
    if (off + ac * 4u > n) {
        *why = "RSN AKM list truncated";
        return -1;
    }
    int psk = iwx_rsn_suite_has(ie + off, ac, 2);
    off += ac * 4u;
    uint16_t caps = off + 2 <= n ? (uint16_t)(ie[off] | ie[off + 1] << 8) : 0;
    if (!ccmp) {
        *why = "AP has no CCMP pairwise cipher";
        return -1;
    }
    if (!psk) {
        *why = "AP has no WPA2-PSK AKM (SAE-only?)";
        return -1;
    }
    if (caps & 0x0040) {
        *why = "AP requires management frame protection";
        return -1;
    }
    static const uint8_t ccmp_s[4] = {0x00, 0x0f, 0xac, 4};
    static const uint8_t psk_s[4] = {0x00, 0x0f, 0xac, 2};
    uint8_t *p = out;
    *p++ = 48;
    *p++ = 20;
    *p++ = 1;
    *p++ = 0;
    memcpy(p, group, 4);
    p += 4;
    *p++ = 1;
    *p++ = 0;
    memcpy(p, ccmp_s, 4);
    p += 4;
    *p++ = 1;
    *p++ = 0;
    memcpy(p, psk_s, 4);
    p += 4;
    *p++ = 0;
    *p++ = 0;
    return (int)(p - out);
}

static struct iwx_bss *iwx_pick_ap(struct iwx_sc *sc, const char *ssid)
{
    struct iwx_bss *best = NULL;
    for (int i = 0; i < sc->nbss; i++) {
        struct iwx_bss *b = &sc->bss[i];
        if (b->ssid_len == 0 || b->ssid[0] == 0)
            continue;
        if (ssid && ssid[0] && strcmp(b->ssid, ssid) != 0)
            continue;
        int open = !b->rsn && !b->wpa && !(b->capinfo & 0x10);
        uint8_t tmp[64];
        const char *why = NULL;
        if (!open && !(b->rsn && iwx_build_rsn(b, tmp, &why) > 0))
            continue;
        if (!best || b->rssi > best->rssi)
            best = b;
    }
    return best;
}

static int iwx_cond_mlme(struct iwx_sc *sc)
{
    dot11_mlme_poll(&sc->mlme, timer_ticks());
    uint8_t st = dot11_mlme_state(&sc->mlme);
    return st == DOT11_MLME_ASSOCIATED || st == DOT11_MLME_FAILED;
}

static int iwx_cond_sp(struct iwx_sc *sc) { return sc->sp_started; }
static int iwx_cond_eapol(struct iwx_sc *sc) { return sc->eapol_seen > 0; }

static void iwx_teardown(struct iwx_sc *sc)
{
    if (sc->mq_enabled && !sc->fatal && dot11_mlme_state(&sc->mlme) == DOT11_MLME_ASSOCIATED) {
        uint32_t before = sc->mq_txresp;
        dot11_mlme_stop(&sc->mlme);
        uint64_t t0 = timer_ticks();
        while (sc->mq_txresp == before && timer_ticks() - t0 < 200 && iwx_service(sc) == 0)
            scheduler_yield();
        klogf(KLOG_INFO, "iwx: disassociation sent");
    }
    sc->mlme_on = 0;
    sc->link_up = 0;
    if (!sc->fatal) {
        if (sc->c_assoc)
            iwx_mac_ctxt(sc, IWX_FW_CTXT_ACTION_MODIFY, 0);
        if (sc->mq_enabled)
            iwx_disable_mgmt_queue(sc);
        if (sc->c_sp && !sc->sp_ended)
            iwx_session_protect(sc, IWX_FW_CTXT_ACTION_REMOVE, 0);
        if (sc->c_sta)
            iwx_rm_sta(sc);
        if (sc->c_bind)
            iwx_binding(sc, IWX_FW_CTXT_ACTION_REMOVE);
        if (sc->c_mac)
            iwx_mac_ctxt(sc, IWX_FW_CTXT_ACTION_REMOVE, 0);
        if (sc->c_phy)
            iwx_phy_ctxt(sc, IWX_FW_CTXT_ACTION_REMOVE);
    }
    sc->c_phy = sc->c_mac = sc->c_bind = sc->c_sta = sc->c_sp = sc->c_assoc = 0;
    sc->mq_enabled = 0;
    sc->ap = NULL;
}

static int iwx_associate(struct iwx_sc *sc, const char *ssid, bool hold)
{
    uint8_t rsn[64];
    int rsn_len = 0;
    uint64_t elapsed = 0;
    int err;

    sc->ap = iwx_pick_ap(sc, ssid);
    if (!sc->ap) {
        klogf(KLOG_ERROR, "iwx: network \"%s\" not found or unsupported (need open or WPA2-PSK/CCMP)",
              ssid ? ssid : "");
        return IWX_EINVAL;
    }
    struct iwx_bss *ap = sc->ap;
    if (ap->rsn) {
        const char *why = NULL;
        rsn_len = iwx_build_rsn(ap, rsn, &why);
        if (rsn_len < 0) {
            klogf(KLOG_ERROR, "iwx: cannot build RSN IE: %s", why);
            return IWX_EINVAL;
        }
    }
    int is2g = ap->channel <= 14;
    sc->lmac_id = (is2g || !isset(sc->enabled_capa, IWX_UCODE_TLV_CAPA_CDB_SUPPORT)) ? IWX_LMAC_24G_INDEX
                                                                                    : IWX_LMAC_5G_INDEX;
    uint8_t txv = iwx_lookup_notif_ver(sc, IWX_LONG_GROUP, IWX_TX_CMD);
    sc->rate_v2 = txv != IWX_FW_CMD_VER_UNKNOWN && txv > 6;
    klogf(KLOG_INFO, "iwx: associating with \"%s\" %02x:%02x:%02x:%02x:%02x:%02x ch %u %d dBm %s, bi %u dtim %u/%u, cap 0x%04x",
          ap->ssid, ap->bssid[0], ap->bssid[1], ap->bssid[2], ap->bssid[3], ap->bssid[4], ap->bssid[5],
          ap->channel, ap->rssi, rsn_len > 0 ? "WPA2-PSK/CCMP" : "open", ap->bintval, ap->dtim_count,
          ap->dtim_period, ap->capinfo);
    klogf(KLOG_INFO, "iwx: versions: phy %u rlc %u scd %u tx-notif %u (rate v%d), uhb %d cdb %d lmac %d",
          iwx_lookup_cmd_ver(sc, IWX_LONG_GROUP, IWX_PHY_CONTEXT_CMD),
          iwx_lookup_cmd_ver(sc, IWX_DATA_PATH_GROUP, IWX_RLC_CONFIG_CMD),
          iwx_lookup_cmd_ver(sc, IWX_DATA_PATH_GROUP, IWX_SCD_QUEUE_CONFIG_CMD), txv,
          sc->rate_v2 ? 2 : 1, !!isset(sc->enabled_capa, IWX_UCODE_TLV_CAPA_ULTRA_HB_CHANNELS),
          !!isset(sc->enabled_capa, IWX_UCODE_TLV_CAPA_CDB_SUPPORT), sc->lmac_id);

    if ((err = iwx_phy_ctxt(sc, IWX_FW_CTXT_ACTION_ADD)))
        goto out;
    sc->c_phy = 1;
    if ((err = iwx_mac_ctxt(sc, IWX_FW_CTXT_ACTION_ADD, 0)))
        goto out;
    sc->c_mac = 1;
    if ((err = iwx_binding(sc, IWX_FW_CTXT_ACTION_ADD)))
        goto out;
    sc->c_bind = 1;
    if ((err = iwx_add_sta(sc, 0)))
        goto out;
    sc->c_sta = 1;
    if ((err = iwx_enable_mgmt_queue(sc)))
        goto out;
    sc->sp_started = sc->sp_ended = 0;
    if ((err = iwx_session_protect(sc, IWX_FW_CTXT_ACTION_ADD, (uint32_t)(ap->bintval ? ap->bintval : 100) * 9)))
        goto out;
    sc->c_sp = 1;
    if (iwx_wait(sc, iwx_cond_sp, 500, &elapsed) == 0)
        klogf(KLOG_OK, "iwx: on channel %u after %llu ms", ap->channel, (unsigned long long)elapsed);
    else
        klogf(KLOG_WARN, "iwx: no session protection start within 500 ms, sending anyway");

    dot11_mlme_init(&sc->mlme, sc->mac, iwx_mlme_tx, sc, NULL, NULL, sc);
    if (rsn_len > 0)
        dot11_mlme_set_rsn_ie(&sc->mlme, rsn, (uint8_t)rsn_len);
    sc->eapol_seen = 0;
    sc->mlme_on = 1;
    if (!dot11_mlme_start_open(&sc->mlme, ap->ssid, ap->bssid, ap->channel, timer_ticks())) {
        klogf(KLOG_ERROR, "iwx: MLME start failed: %s", dot11_mlme_fail_name(sc->mlme.fail_reason));
        err = IWX_EIO;
        goto out;
    }
    err = iwx_wait(sc, iwx_cond_mlme, 6000, &elapsed);
    uint8_t st = dot11_mlme_state(&sc->mlme);
    if (st != DOT11_MLME_ASSOCIATED) {
        klogf(KLOG_ERROR, "iwx: association failed after %llu ms: state %s, reason %s, status %u, tx responses %u",
              (unsigned long long)elapsed, dot11_mlme_state_name(st),
              dot11_mlme_fail_name(sc->mlme.fail_reason), sc->mlme.last_status, sc->mq_txresp);
        err = IWX_EIO;
        goto out;
    }
    klogf(KLOG_OK, "iwx: ASSOCIATED with \"%s\" in %llu ms, aid %u", ap->ssid,
          (unsigned long long)elapsed, sc->mlme.aid);

    if ((err = iwx_add_sta(sc, 1)))
        goto out;
    if ((err = iwx_mac_ctxt(sc, IWX_FW_CTXT_ACTION_MODIFY, 1)))
        goto out;
    sc->c_assoc = 1;
    sc->assoc_aid = sc->mlme.aid;

    if (rsn_len > 0) {
        if (iwx_wait(sc, iwx_cond_eapol, 3000, &elapsed) == 0)
            klogf(KLOG_OK, "iwx: EAPOL message 1 from AP received %llu ms after association",
                  (unsigned long long)elapsed);
        else
            klogf(KLOG_WARN, "iwx: no EAPOL frame from AP within 3 s");
        klogf(KLOG_WARN, "iwx: WPA2 4-way handshake is not implemented yet, disconnecting");
        err = IWX_EHANDSHAKE;
        goto out;
    }
    err = 0;

out:
    if (err || !hold)
        iwx_teardown(sc);
    return err;
}

static void iwx_stop_device(struct iwx_sc *sc)
{
    iwx_disable_interrupts(sc);
    iwx_disable_rx_dma(sc);
    sc->rx_cur = 0;
    if (sc->rx_stat.va)
        *(volatile uint16_t *)sc->rx_stat.va = 0;
    if (sc->tx_desc.va)
        iwx_reset_tx_ring(sc);
    iwx_clrbits(sc, IWX_CSR_GP_CNTRL, IWX_CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);
    if (sc->nic_locks > 0)
        klogf(KLOG_WARN, "iwx: %d active NIC locks forcefully cleared", sc->nic_locks);
    sc->nic_locks = 0;
    iwx_apm_stop(sc);
    iwx_sw_reset(sc);
    iwx_conf_msix_hw(sc, 1);
    iwx_disable_interrupts(sc);
    iwx_enable_rfkill_int(sc);
    iwx_check_rfkill(sc);
    iwx_prepare_card_hw(sc);
    klogf(KLOG_INFO, "iwx: device stopped GP_CNTRL=0x%08x", iwx_read(sc, IWX_CSR_GP_CNTRL));
}

static void iwx_pci_visitor(const struct pci_device_info *dev, void *ctx)
{
    struct iwx_sc *sc = ctx;
    if (sc->found)
        return;
    if (dev->vendor_id == 0x8086 && dev->device_id == 0x51f0 && dev->class_code == 0x02) {
        sc->pci = *dev;
        sc->found = true;
    }
}

static bool iwx_attach(struct iwx_sc *sc)
{
    uint32_t ss = pci_read_config32(sc->pci.bus, sc->pci.slot, sc->pci.function, 0x2c);
    sc->subsys_vendor = (uint16_t)(ss & 0xffff);
    sc->subsys_device = (uint16_t)(ss >> 16);
    klogf(KLOG_INFO, "iwx: found %04x:%04x at %02x:%02x.%u subsys %04x:%04x rev 0x%02x",
          sc->pci.vendor_id, sc->pci.device_id, sc->pci.bus, sc->pci.slot, sc->pci.function,
          sc->subsys_vendor, sc->subsys_device, sc->pci.revision);

    sc->pcie_cap = iwx_find_pcie_cap(sc);
    if (!sc->pcie_cap) {
        klogf(KLOG_ERROR, "iwx: PCIe capability structure not found!");
        return false;
    }

    uint32_t reg = pci_read_config32(sc->pci.bus, sc->pci.slot, sc->pci.function, 0x40);
    pci_write_config32(sc->pci.bus, sc->pci.slot, sc->pci.function, 0x40, reg & ~0xff00u);

    sc->bar_pa = pci_read_bar(sc->pci.bus, sc->pci.slot, sc->pci.function, 0);
    if (!sc->bar_pa) {
        klogf(KLOG_ERROR, "iwx: BAR0 not assigned");
        return false;
    }
    pci_update_command(&sc->pci, 0, PCI_COMMAND_MEMORY);
    sc->bar_size = pci_bar_size(sc->pci.bus, sc->pci.slot, sc->pci.function, 0);
    if (sc->bar_size == 0 || sc->bar_size > 0x100000)
        sc->bar_size = 0x4000;
    pci_update_command(&sc->pci, PCI_COMMAND_MEMORY | PCI_COMMAND_BUS_MASTER |
                                      IWX_PCI_COMMAND_INTX_DISABLE, 0);
    sc->regs = (volatile uint8_t *)mmio_map(sc->bar_pa, sc->bar_size);
    if (!sc->regs) {
        klogf(KLOG_ERROR, "iwx: can't map mem space 0x%llx", (unsigned long long)sc->bar_pa);
        return false;
    }
    klogf(KLOG_INFO, "iwx: BAR0 0x%llx size 0x%llx, PCI cmd/status 0x%08x, pcie cap 0x%02x",
          (unsigned long long)sc->bar_pa, (unsigned long long)sc->bar_size,
          pci_read_config32(sc->pci.bus, sc->pci.slot, sc->pci.function, 0x04), sc->pcie_cap);

    iwx_write(sc, IWX_CSR_INT_MASK, 0);
    iwx_write(sc, IWX_CSR_INT, ~0u);
    iwx_write(sc, IWX_CSR_FH_INT_STATUS, ~0u);

    sc->hw_rev_raw = iwx_read(sc, IWX_CSR_HW_REV);
    sc->hw_rf_id = iwx_read(sc, IWX_CSR_HW_RF_ID);
    if (sc->hw_rev_raw == 0xffffffff) {
        klogf(KLOG_ERROR, "iwx: CSR_HW_REV reads all ones, device not accessible");
        return false;
    }
    sc->hw_rev = (sc->hw_rev_raw & 0xfff0) | (IWX_CSR_HW_REV_STEP(sc->hw_rev_raw << 2) << 2);
    klogf(KLOG_INFO, "iwx: CSR_HW_REV 0x%08x (hw_rev 0x%x mac type 0x%x step %u) RF_ID 0x%08x (rf type 0x%x)",
          sc->hw_rev_raw, sc->hw_rev, IWX_CSR_HW_REV_TYPE(sc->hw_rev),
          IWX_CSR_HW_REV_STEP(sc->hw_rev << 2), sc->hw_rf_id, IWX_CSR_HW_RFID_TYPE(sc->hw_rf_id));
    if (IWX_CSR_HW_REV_TYPE(sc->hw_rev) != IWX_CFG_MAC_TYPE_SO ||
        IWX_CSR_HW_RFID_TYPE(sc->hw_rf_id) != IWX_CFG_RF_TYPE_HR2) {
        klogf(KLOG_ERROR, "iwx: unsupported mac/rf combination for %s", IWX_FW_NAME);
        return false;
    }

    sc->integrated = 1;
    sc->imr_enabled = 1;
    sc->mac_addr_from_csr = 0x380;
    sc->umac_prph_offset = 0x300000;
    sc->max_tfd_queue_size = IWX_TFD_QUEUE_SIZE_MAX_GEN3;

    if (iwx_apm_init(sc) == 0 && iwx_nic_lock(sc)) {
        iwx_get_crf_id(sc);
        iwx_nic_unlock(sc);
        iwx_apm_stop(sc);
    }
    klogf(KLOG_INFO, "iwx: crf-id 0x%08x cnv-id 0x%08x pm_support %d ltr %d", sc->hw_crf_id,
          sc->hw_cnv_id, sc->pm_support, sc->ltr_enabled);

    if (!iwx_dma_alloc(&sc->ctxt_info, sizeof(struct iwx_context_info_gen3), "ctxt info"))
        return false;
    if (!iwx_dma_alloc(&sc->prph_scratch, sizeof(struct iwx_prph_scratch), "prph scratch"))
        return false;
    if (!iwx_dma_alloc(&sc->prph_info, IWX_PAGE_SIZE, "prph info"))
        return false;
    if (!iwx_alloc_tx_ring(sc))
        return false;
    if (!iwx_dma_alloc(&sc->tx_big, IWX_PAGE_SIZE, "tx big cmd"))
        return false;
    if (!iwx_dma_alloc(&sc->mq_desc, IWX_TX_RING_COUNT * sizeof(struct iwx_tfh_tfd), "mgmt tfd ring") ||
        !iwx_dma_alloc(&sc->mq_bc, sizeof(struct iwx_gen3_bc_tbl_entry) *
                                       IWX_TFD_QUEUE_BC_SIZE_GEN3_AX210, "mgmt bc table") ||
        !iwx_dma_alloc(&sc->mq_cmd, IWX_TX_RING_COUNT * sizeof(struct iwx_device_cmd), "mgmt cmd") ||
        !iwx_dma_alloc(&sc->mq_buf, IWX_TX_RING_COUNT * IWX_MQ_BUF, "mgmt payload"))
        return false;
    if (!iwx_alloc_rx_ring(sc))
        return false;
    klogf(KLOG_INFO, "iwx: rings allocated: tx %d x %u B, cmd %u B, rx %d x 4K",
          IWX_TX_RING_COUNT, (unsigned)sizeof(struct iwx_tfh_tfd),
          (unsigned)sizeof(struct iwx_device_cmd), IWX_RX_MQ_RING_COUNT);
    return true;
}

static void iwx_service_loop(struct iwx_sc *sc);

static void iwx_thread(void *arg)
{
    struct iwx_sc *sc = arg;
    int err;

    pci_enumerate(iwx_pci_visitor, sc);
    if (!sc->found) {
        klogf(KLOG_INFO, "iwx: no supported device");
        return;
    }
    iwx_calibrate();
    if (!iwx_attach(sc))
        return;
    if (!iwx_load_firmware_file(sc))
        return;
    if (iwx_read_firmware(sc))
        return;

    err = iwx_prepare_card_hw(sc);
    if (err) {
        klogf(KLOG_ERROR, "iwx: could not initialize hardware");
        return;
    }
    err = iwx_start_hw(sc);
    if (err) {
        klogf(KLOG_ERROR, "iwx: could not initialize hardware (%d)", err);
        iwx_stop_device(sc);
        return;
    }
    err = iwx_run_init_mvm_ucode(sc, 1);
    iwx_stop_device(sc);
    if (err) {
        klogf(KLOG_ERROR, "iwx: init firmware run failed (%d)", err);
        return;
    }
    klogf(KLOG_OK, "iwx: milestone 1 complete: hw rev 0x%x, fw %u.%08x.%u, address %02x:%02x:%02x:%02x:%02x:%02x",
          sc->hw_rev & IWX_CSR_HW_REV_TYPE_MSK, sc->fw_major, sc->fw_minor, sc->fw_api,
          sc->mac[0], sc->mac[1], sc->mac[2], sc->mac[3], sc->mac[4], sc->mac[5]);

    err = iwx_start_hw(sc);
    if (err) {
        klogf(KLOG_ERROR, "iwx: runtime start_hw failed (%d)", err);
        iwx_stop_device(sc);
        return;
    }
    err = iwx_run_init_mvm_ucode(sc, 0);
    if (!err)
        err = iwx_runtime_config(sc);
    if (!err)
        err = iwx_scan(sc);
    if (err) {
        iwx_stop_device(sc);
        klogf(KLOG_ERROR, "iwx: milestone 2 failed (%d)", err);
        return;
    }
    klogf(KLOG_OK, "iwx: milestone 2 complete: %d networks found", sc->nbss);
    iwx_service_loop(sc);
    iwx_stop_device(sc);
}

static int iwx_wifi_security(const struct iwx_bss *b)
{
    if (b->rsn && b->rsn_len >= 10) {
        const uint8_t *ie = b->rsn_ie;
        uint32_t off = 8;
        uint16_t pc = (uint16_t)(ie[off] | ie[off + 1] << 8);
        off += 2 + pc * 4u;
        if (off + 2 <= b->rsn_len) {
            uint16_t ac = (uint16_t)(ie[off] | ie[off + 1] << 8);
            off += 2;
            if (off + ac * 4u <= b->rsn_len) {
                int psk = iwx_rsn_suite_has(ie + off, ac, 2) || iwx_rsn_suite_has(ie + off, ac, 6);
                int sae = iwx_rsn_suite_has(ie + off, ac, 8);
                if (psk && sae)
                    return WIFI_SECURITY_WPA2_WPA3;
                if (sae)
                    return WIFI_SECURITY_WPA3;
            }
        }
        return WIFI_SECURITY_WPA2;
    }
    if (b->rsn || b->wpa)
        return WIFI_SECURITY_WPA2;
    if (b->capinfo & 0x10)
        return WIFI_SECURITY_WEP;
    return WIFI_SECURITY_OPEN;
}

static bool iwx_net_transmit(void *ctx, const uint8_t *frame, uint16_t length)
{
    struct iwx_sc *sc = ctx;
    if (!sc->link_up || !iwx_swq_put(&iwx_txsw, frame, length)) {
        sc->net.stats.tx_dropped++;
        return false;
    }
    sc->net.stats.tx_bytes += length;
    return true;
}

static void iwx_net_poll(void *ctx, uint32_t budget)
{
    struct iwx_sc *sc = ctx;
    static uint8_t eth[IWX_SWQ_BYTES];
    uint32_t n = 0;
    while (n < (budget ? budget : 32)) {
        uint32_t len = 0;
        const uint8_t *frame = iwx_swq_peek(&iwx_rxsw, &len);
        if (!frame)
            break;
        const struct iwx_bss *ap = sc->ap;
        uint16_t elen = ap ? dot11_data_to_ethernet(frame, (uint16_t)len, ap->bssid, eth, sizeof(eth)) : 0;
        iwx_swq_pop(&iwx_rxsw);
        n++;
        if (!elen) {
            sc->net.stats.rx_dropped++;
            continue;
        }
        sc->net.stats.rx_packets++;
        sc->net.stats.rx_bytes += elen;
        if (!net_device_receive(&sc->net, eth, elen))
            sc->net.stats.rx_dropped++;
    }
}

static void iwx_drain_tx(struct iwx_sc *sc)
{
    static uint8_t buf[IWX_SWQ_BYTES + 64];
    for (int n = 0; n < 32; n++) {
        uint32_t len = 0;
        const uint8_t *eth = iwx_swq_peek(&iwx_txsw, &len);
        if (!eth)
            return;
        if (!sc->link_up || !sc->ap) {
            iwx_swq_pop(&iwx_txsw);
            continue;
        }
        uint16_t seq = (uint16_t)(sc->tx_seq++ << 4);
        uint16_t flen = dot11_ethernet_to_data(eth, (uint16_t)len, sc->ap->bssid, seq, buf, sizeof(buf));
        if (!flen) {
            iwx_swq_pop(&iwx_txsw);
            sc->net.stats.tx_dropped++;
            continue;
        }
        if (!iwx_tx_frame(sc, buf, flen, 0))
            return;
        iwx_swq_pop(&iwx_txsw);
    }
}

static bool iwx_net_link_up(void *ctx)
{
    return ((struct iwx_sc *)ctx)->link_up != 0;
}

static const struct net_device_ops iwx_net_ops = {
    .transmit = iwx_net_transmit,
    .poll = iwx_net_poll,
    .link_up = iwx_net_link_up,
};

static bool iwx_wifi_scan(void *ctx)
{
    struct iwx_sc *sc = ctx;
    if (sc->fatal || sc->link_up || sc->scan_ready)
        return false;
    sc->req_scan = 1;
    return true;
}

static bool iwx_wifi_connect(void *ctx, const char *ssid, const char *password)
{
    struct iwx_sc *sc = ctx;
    if (sc->fatal || sc->req_connect)
        return false;
    strncpy(sc->req_ssid, ssid, sizeof(sc->req_ssid) - 1);
    sc->req_ssid[sizeof(sc->req_ssid) - 1] = 0;
    strncpy(sc->req_pass, password ? password : "", sizeof(sc->req_pass) - 1);
    sc->req_pass[sizeof(sc->req_pass) - 1] = 0;
    sc->conn_failed = 0;
    sc->req_connect = 1;
    return true;
}

static bool iwx_wifi_disconnect(void *ctx)
{
    struct iwx_sc *sc = ctx;
    sc->req_disconnect = 1;
    return true;
}

static void iwx_wifi_poll(void *ctx, uint64_t now_ms)
{
    struct iwx_sc *sc = ctx;
    (void)now_ms;
    if (sc->scan_ready) {
        for (int i = 0; i < sc->nbss; i++) {
            const struct iwx_bss *b = &sc->bss[i];
            if (!b->ssid_len || !b->ssid[0])
                continue;
            struct wifi_network n;
            memset(&n, 0, sizeof(n));
            memcpy(n.ssid, b->ssid, b->ssid_len);
            memcpy(n.bssid, b->bssid, 6);
            n.rssi = b->rssi;
            n.channel = b->channel;
            n.security = (uint8_t)iwx_wifi_security(b);
            (void)wifi_report_scan_result(&n);
        }
        wifi_notify_scan_done();
        sc->scan_ready = 0;
    }
    if (sc->conn_failed) {
        sc->conn_failed = 0;
        wifi_notify_connect_failed(sc->conn_error);
    }
    if (sc->lost_report) {
        sc->lost_report = 0;
        (void)wifi_disconnect();
    }
    if (sc->link_up && sc->ap)
        wifi_update_rssi(sc->ap->rssi);
}

static bool iwx_wifi_is_connected(void *ctx)
{
    return ((struct iwx_sc *)ctx)->link_up != 0;
}

static const struct wifi_ops iwx_wifi_ops = {
    .scan = iwx_wifi_scan,
    .connect = iwx_wifi_connect,
    .disconnect = iwx_wifi_disconnect,
    .poll = iwx_wifi_poll,
    .is_connected = iwx_wifi_is_connected,
};

static void iwx_service_loop(struct iwx_sc *sc)
{
    memcpy(sc->net.mac, sc->mac, 6);
    strncpy(sc->net.name, "wlan0", sizeof(sc->net.name) - 1);
    sc->net.mtu = NET_ETHERNET_MTU;
    sc->net.ops = &iwx_net_ops;
    sc->net.driver_context = sc;
    sc->net.cached_link_up = false;
    if (!net_device_register(&sc->net)) {
        klogf(KLOG_ERROR, "iwx: net_device_register failed");
        return;
    }
    if (!wifi_device_register(&sc->net, &iwx_wifi_ops, sc, "wlan0")) {
        klogf(KLOG_ERROR, "iwx: wifi_device_register failed");
        return;
    }
    sc->scan_ready = 1;
    klogf(KLOG_OK, "iwx: wlan0 ready, waiting for scan and connect requests");
    for (;;) {
        if (iwx_service(sc) < 0) {
            klogf(KLOG_ERROR, "iwx: firmware died, Wi-Fi disabled until reboot");
            sc->link_up = 0;
            sc->conn_error = -30;
            sc->conn_failed = 1;
            return;
        }
        if (sc->link_up && !sc->link_lost && dot11_mlme_state(&sc->mlme) != DOT11_MLME_ASSOCIATED) {
            klogf(KLOG_WARN, "iwx: access point ended the association (%s, reason %u)",
                  dot11_mlme_fail_name(sc->mlme.fail_reason), sc->mlme.last_status);
            sc->link_lost = 1;
        }
        if (sc->link_lost) {
            sc->busy = 1;
            iwx_teardown(sc);
            iwx_swq_reset(&iwx_txsw);
            iwx_swq_reset(&iwx_rxsw);
            sc->link_lost = 0;
            sc->lost_report = 1;
            sc->busy = 0;
        }
        if (sc->link_up)
            iwx_drain_tx(sc);
        if (sc->req_disconnect) {
            sc->req_disconnect = 0;
            if (sc->ap || sc->link_up) {
                klogf(KLOG_INFO, "iwx: disconnect requested");
                sc->busy = 1;
                iwx_teardown(sc);
                sc->busy = 0;
            }
        }
        if (sc->req_connect) {
            sc->busy = 1;
            sc->req_connect = 0;
            if (sc->ap || sc->link_up)
                iwx_teardown(sc);
            klogf(KLOG_INFO, "iwx: connect requested to \"%s\"", sc->req_ssid);
            int err = iwx_associate(sc, sc->req_ssid, true);
            if (err) {
                sc->conn_error = err == IWX_EHANDSHAKE ? -20 : -21;
                sc->conn_failed = 1;
            } else {
                iwx_swq_reset(&iwx_txsw);
                iwx_swq_reset(&iwx_rxsw);
                sc->tx_seq = 0;
                sc->link_up = 1;
                klogf(KLOG_OK, "iwx: link up on \"%s\", data path enabled", sc->req_ssid);
            }
            sc->busy = 0;
        }
        if (sc->req_scan && !sc->link_up && !sc->ap) {
            sc->busy = 1;
            sc->req_scan = 0;
            if (iwx_scan(sc) == 0 || sc->nbss)
                sc->scan_ready = 1;
            sc->busy = 0;
        }
        scheduler_sleep(5);
    }
}

bool iwx_start(void)
{
    if (iwx_started)
        return true;
    memset(&iwx_softc, 0, sizeof(iwx_softc));
    if (scheduler_create_thread(iwx_thread, &iwx_softc, "iwx", 1, -1) < 0) {
        klogf(KLOG_ERROR, "iwx: cannot create thread");
        return false;
    }
    iwx_started = true;
    return true;
}
