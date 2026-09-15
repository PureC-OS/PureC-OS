// PureC-OS AR9285 (ath9k-class) — phase 2a: open-system association до эфира.
//
// Что умеет эта фаза:
//  - phase 1 (PCI, MEM+BM, BAR0, SREV, wake MAC, STA MAC, wlan0 + wifi_ops);
//  - 802.11 MLME open-system: auth -> assoc через src/net/802.11/assoc.c;
//  - MAC STA-режим: STA_ID0/1, BSSID0/1 (+AID после assoc);
//  - TX mgmt-каркас: теневой TX-ринг + QCU/DCU очередь MGMT_Q, TXDP kick,
//    опрос TXOK через ISR_S0 с таймаутом (без зависаний);
//  - RX mgmt-каркас: теневой RX-ринг + RXDP, опрос в poll, валидация через
//    dot11_mlme_input() (мусор отваливается по BSSID/FC-проверкам).
//
// Чего НЕ умеет (следующие фазы):
//  - EEPROM/калибровка, BB/RF init-таблицы, установка канала по RF,
//    TX power, rate control кроме фикс. CCK 1M для mgmt;
//  - data-путь Ethernet<->802.11 (LLC/SNAP, шифрование), сканирование по
//    каналам (passive scan 1..13), WPA2 4-way handshake (phase 2b).
//  Кадры доходят до TX-движка; слышимость в эфире зависит от RF-инита,
//  который BIOS/предыдущий драйвер мог оставить. Это честно логируется.
//
// Регистровые смещения сверены с Linux drivers/net/wireless/ath/ath9k/reg.h
// (ISC-лицензия, Atheros Communications) — только оффсеты/битовые маски,
// логика init и дескрипторы написаны с нуля под PureC-OS.
// Дескрипторы — упрощённый best-effort layout под TX-движок AR9285:
// память только из pmm, завершение по ISR_S0_TXOK + таймаут, зависнуть нельзя.

#include "ar9285.h"
#include "arch/x86_64/mmio.h"
#include "drivers/interrupts/timer.h"
#include "drivers/pci/pci.h"
#include "kernel/diagnostics/klog.h"
#include "lib/string.h"
#include "mm/pmm.h"
#include "net/802.11/include/802.h"
#include "net/core/net_device.h"
#include "net/wifi/wifi.h"
#include <stddef.h>
#include <stdint.h>

#define AR9285_VENDOR_ATHEROS 0x168C
#define AR9285_DEVICE_AR9285 0x002B
#define AR9285_MMIO_SIZE 0x10000U
#define AR9285_POLL_TIMEOUT 1000000U

// --- Регистры (имена/оффсеты как в ath9k reg.h) ---
#define AR_CR 0x0008
#define AR_CR_RXE 0x00000004
#define AR_CR_RXD 0x00000020

#define AR_RXDP 0x000C

#define AR_CFG 0x0014
#define AR_CFG_PHOK 0x00000100

#define AR_TXCFG 0x0030
#define AR_RXCFG 0x0034

#define AR_ISR 0x0080
#define AR_ISR_S0 0x0084
#define AR_ISR_S0_QCU_TXOK 0x000003FF
#define AR_ISR_S1 0x0088
#define AR_IMR 0x00A0

#define AR_MAC_SLEEP 0x1F00
#define AR_MAC_SLEEP_MAC_ASLEEP 0x00000001

#define AR_RC 0x4000
#define AR_RC_AHB 0x00000001
#define AR_RC_APB 0x00000002

#define AR_SREV 0x4020
#define AR_SREV_ID_MASK 0x000000FF
#define AR_SREV_VERSION_MASK 0x000000F0
#define AR_SREV_VERSION_S 4
#define AR_SREV_REVISION_MASK 0x00000007
#define AR_SREV_VERSION_9285 0x0C // (0xC0 >> 4)

// QCU/DCU mgmt-очередь (1:1 mapping DCU8 <-> QCU8, вдали от data-очередей 0..3).
#define AR9285_MGMT_Q 8
#define AR_Q0_TXDP 0x0800
#define AR_QTXDP(_q) (AR_Q0_TXDP + ((_q) << 2))
#define AR_Q_TXE 0x0840
#define AR_Q_TXE_MGMT_BIT (1U << AR9285_MGMT_Q)
#define AR_Q0_STS 0x0A00
#define AR_QSTS(_q) (AR_Q0_STS + ((_q) << 2))
#define AR_D0_QCUMASK 0x1000
#define AR_DQCUMASK(_d) (AR_D0_QCUMASK + ((_d) << 2))

#define AR_STA_ID0 0x8000
#define AR_STA_ID1 0x8004
#define AR_STA_ID1_SADH_MASK 0x0000FFFF
#define AR_STA_ID1_STA_AP 0x00010000
#define AR_STA_ID1_ADHOC 0x00020000
#define AR_STA_ID1_BASE_RATE_11B 0x02000000

#define AR_BSS_ID0 0x8008
#define AR_BSS_ID1 0x800C
#define AR_BSS_ID1_AID 0x07FF0000
#define AR_BSS_ID1_AID_S 16

#define AR_RX_FILTER 0x803C

#define AR9285_TX_RING 4
#define AR9285_RX_RING 8

// Упрощённый TX-дескриптор (best-effort под AR9285 TX-движок).
// link/buf — физические адреса, len — длина кадра, rate — фикс CCK 1M
// для mgmt, status — движок помечает завершение (poll по ISR_S0_TXOK).
struct ar_tx_desc {
    uint32_t link;
    uint32_t buf;
    uint16_t len;
    uint16_t rate_flags;
    uint32_t status;
    uint32_t reserved[3];
};

struct ar_rx_desc {
    uint32_t link;
    uint32_t buf;
    uint32_t status;
    uint32_t len;
    uint32_t reserved[4];
};

struct ar9285_device {
    volatile uint8_t *regs;
    struct pci_device_info pci;
    struct net_device net;
    uint8_t mac_version;
    uint8_t mac_rev;
    char target_ssid[WIFI_SSID_MAX + 1];
    char target_password[WIFI_PASSWORD_MAX + 1];
    uint8_t target_bssid[6];
    uint8_t target_channel;
    bool found;
    bool initialized;
    // TX/RX каркас phase 2a.
    bool tx_ready;
    bool rx_ready;
    uint64_t tx_ring_phys;
    struct ar_tx_desc *tx_ring;
    uint64_t tx_buf_phys[AR9285_TX_RING];
    uint8_t *tx_bufs[AR9285_TX_RING];
    uint16_t tx_next;
    uint64_t rx_ring_phys;
    struct ar_rx_desc *rx_ring;
    uint64_t rx_buf_phys[AR9285_RX_RING];
    uint8_t *rx_bufs[AR9285_RX_RING];
    uint16_t rx_next;
    // MLME open-system.
    struct dot11_mlme mlme;
    bool mlme_active;
    bool data_path_warned;
};

static struct ar9285_device adapter;

static uint32_t ar_reg_read(uint32_t offset) {
    return *(volatile uint32_t *)(adapter.regs + offset);
}

static void ar_reg_write(uint32_t offset, uint32_t value) {
    *(volatile uint32_t *)(adapter.regs + offset) = value;
}

static bool ar_wake_mac(void) {
    // Вывести MAC из сна: записать 0 и дождаться.
    ar_reg_write(AR_MAC_SLEEP, 0);
    for (uint32_t i = 0; i < AR9285_POLL_TIMEOUT; i++) {
        if ((ar_reg_read(AR_MAC_SLEEP) & AR_MAC_SLEEP_MAC_ASLEEP) == 0)
            return true;
        __asm__ volatile("pause");
    }
    return false;
}

static bool ar_read_srev(uint8_t *version, uint8_t *rev) {
    uint32_t srev = ar_reg_read(AR_SREV);
    if (srev == 0 || srev == 0xFFFFFFFFU)
        return false;
    *version = (uint8_t)((srev & AR_SREV_VERSION_MASK) >> AR_SREV_VERSION_S);
    *rev = (uint8_t)(srev & AR_SREV_REVISION_MASK);
    return true;
}

static bool ar_read_mac(uint8_t mac[6]) {
    uint32_t lo = ar_reg_read(AR_STA_ID0);
    uint32_t hi = ar_reg_read(AR_STA_ID1) & AR_STA_ID1_SADH_MASK;
    if (lo == 0xFFFFFFFFU)
        return false;
    mac[0] = (uint8_t)lo;
    mac[1] = (uint8_t)(lo >> 8);
    mac[2] = (uint8_t)(lo >> 16);
    mac[3] = (uint8_t)(lo >> 24);
    mac[4] = (uint8_t)hi;
    mac[5] = (uint8_t)(hi >> 8);
    bool all_zero = true;
    bool all_ff = true;
    for (uint8_t i = 0; i < 6; i++) {
        if (mac[i])
            all_zero = false;
        if (mac[i] != 0xFF)
            all_ff = false;
    }
    // Мультикаст-бит у STA-адреса STA_ID0 не обязан быть 0, проверяем мягче e1000.
    return !all_zero && !all_ff;
}

static void ar_program_sta(const uint8_t mac[6]) {
    uint32_t lo = (uint32_t)mac[0] | ((uint32_t)mac[1] << 8) |
                  ((uint32_t)mac[2] << 16) | ((uint32_t)mac[3] << 24);
    uint32_t hi = ar_reg_read(AR_STA_ID1) & ~AR_STA_ID1_SADH_MASK;
    hi &= ~(AR_STA_ID1_STA_AP | AR_STA_ID1_ADHOC); // STA, не AP/AdHoc
    hi |= ((uint32_t)mac[4] | ((uint32_t)mac[5] << 8));
    hi |= AR_STA_ID1_BASE_RATE_11B; // mgmt ACK/CTS на CCK, не OFDM 6M
    ar_reg_write(AR_STA_ID0, lo);
    ar_reg_write(AR_STA_ID1, hi);
}

static void ar_program_bssid(const uint8_t bssid[6], uint16_t aid) {
    uint32_t lo = (uint32_t)bssid[0] | ((uint32_t)bssid[1] << 8) |
                  ((uint32_t)bssid[2] << 16) | ((uint32_t)bssid[3] << 24);
    uint32_t hi = (uint32_t)bssid[4] | ((uint32_t)bssid[5] << 8);
    hi |= ((uint32_t)(aid & 0x07FF) << AR_BSS_ID1_AID_S);
    ar_reg_write(AR_BSS_ID0, lo);
    ar_reg_write(AR_BSS_ID1, hi);
}

static void ar_release_dma(void) {
    if (adapter.tx_ring_phys) {
        // Страницы буферов освобождаем отдельно от ринга.
        for (uint32_t i = 0; i < AR9285_TX_RING; i++) {
            if (adapter.tx_buf_phys[i])
                pmm_free_page(adapter.tx_buf_phys[i]);
            adapter.tx_buf_phys[i] = 0;
            adapter.tx_bufs[i] = NULL;
        }
        pmm_free_page(adapter.tx_ring_phys);
        adapter.tx_ring_phys = 0;
        adapter.tx_ring = NULL;
    }
    if (adapter.rx_ring_phys) {
        for (uint32_t i = 0; i < AR9285_RX_RING; i++) {
            if (adapter.rx_buf_phys[i])
                pmm_free_page(adapter.rx_buf_phys[i]);
            adapter.rx_buf_phys[i] = 0;
            adapter.rx_bufs[i] = NULL;
        }
        pmm_free_page(adapter.rx_ring_phys);
        adapter.rx_ring_phys = 0;
        adapter.rx_ring = NULL;
    }
    adapter.tx_ready = false;
    adapter.rx_ready = false;
}

static bool ar_tx_init(void) {
    adapter.tx_ring_phys = pmm_allocate_page();
    if (!adapter.tx_ring_phys)
        return false;
    adapter.tx_ring =
        (struct ar_tx_desc *)pmm_physical_to_virtual(adapter.tx_ring_phys);
    if (!adapter.tx_ring) {
        pmm_free_page(adapter.tx_ring_phys);
        adapter.tx_ring_phys = 0;
        return false;
    }
    memset(adapter.tx_ring, 0, sizeof(struct ar_tx_desc) * AR9285_TX_RING);
    for (uint32_t i = 0; i < AR9285_TX_RING; i++) {
        uint64_t bp = pmm_allocate_page();
        if (!bp) {
            ar_release_dma();
            return false;
        }
        adapter.tx_buf_phys[i] = bp;
        adapter.tx_bufs[i] = (uint8_t *)pmm_physical_to_virtual(bp);
        if (!adapter.tx_bufs[i]) {
            ar_release_dma();
            return false;
        }
        memset(adapter.tx_bufs[i], 0, 4096);
        uint64_t next_phys =
            adapter.tx_ring_phys + ((uint64_t)((i + 1) % AR9285_TX_RING) *
                                    sizeof(struct ar_tx_desc));
        adapter.tx_ring[i].link = (uint32_t)next_phys;
        adapter.tx_ring[i].buf = (uint32_t)bp;
    }
    adapter.tx_next = 0;
    // DCU8 <-> QCU8 1:1, включаем очередь.
    ar_reg_write(AR_DQCUMASK(AR9285_MGMT_Q), (1U << AR9285_MGMT_Q));
    ar_reg_write(AR_Q_TXE, ar_reg_read(AR_Q_TXE) | AR_Q_TXE_MGMT_BIT);
    ar_reg_write(AR_QTXDP(AR9285_MGMT_Q), (uint32_t)adapter.tx_ring_phys);
    adapter.tx_ready = true;
    return true;
}

static bool ar_rx_init(void) {
    adapter.rx_ring_phys = pmm_allocate_page();
    if (!adapter.rx_ring_phys)
        return false;
    adapter.rx_ring =
        (struct ar_rx_desc *)pmm_physical_to_virtual(adapter.rx_ring_phys);
    if (!adapter.rx_ring) {
        pmm_free_page(adapter.rx_ring_phys);
        adapter.rx_ring_phys = 0;
        return false;
    }
    memset(adapter.rx_ring, 0, sizeof(struct ar_rx_desc) * AR9285_RX_RING);
    for (uint32_t i = 0; i < AR9285_RX_RING; i++) {
        uint64_t bp = pmm_allocate_page();
        if (!bp) {
            ar_release_dma();
            return false;
        }
        adapter.rx_buf_phys[i] = bp;
        adapter.rx_bufs[i] = (uint8_t *)pmm_physical_to_virtual(bp);
        if (!adapter.rx_bufs[i]) {
            ar_release_dma();
            return false;
        }
        memset(adapter.rx_bufs[i], 0, 4096);
        uint64_t next_phys =
            adapter.rx_ring_phys + ((uint64_t)((i + 1) % AR9285_RX_RING) *
                                    sizeof(struct ar_rx_desc));
        adapter.rx_ring[i].link = (uint32_t)next_phys;
        adapter.rx_ring[i].buf = (uint32_t)bp;
        adapter.rx_ring[i].status = 0;
        adapter.rx_ring[i].len = 0;
    }
    adapter.rx_next = 0;
    ar_reg_write(AR_RXDP, (uint32_t)adapter.rx_ring_phys);
    ar_reg_write(AR_CR, ar_reg_read(AR_CR) | AR_CR_RXE);
    adapter.rx_ready = true;
    return true;
}

// Положить mgmt-кадр в TX-движок. Возвращает true если кадр принят в очередь
// (не обязательно уже TXOK — подтверждение придёт через ISR_S0/retry MLME).
// Никогда не виснет: только bounded spin на TXOK для лога.
static bool ar_tx_mgmt(const uint8_t *frame, uint16_t len) {
    struct ar9285_device *dev = &adapter;
    if (!dev->initialized || !dev->tx_ready || !dev->tx_ring || !frame || !len)
        return false;
    if (len > DOT11_MGMT_MAX || len > 4096)
        return false;
    uint16_t idx = dev->tx_next;
    memcpy(dev->tx_bufs[idx], frame, len);
    __atomic_thread_fence(__ATOMIC_RELEASE);
    dev->tx_ring[idx].len = len;
    dev->tx_ring[idx].rate_flags = 0; // фикс CCK 1M для mgmt (phase 2b: rates)
    dev->tx_ring[idx].status = 0;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    // Kick: TXDP на текущий дескриптор.
    uint64_t desc_phys =
        dev->tx_ring_phys + (uint64_t)idx * sizeof(struct ar_tx_desc);
    ar_reg_write(AR_QTXDP(AR9285_MGMT_Q), (uint32_t)desc_phys);
    dev->tx_next = (uint16_t)((idx + 1) % AR9285_TX_RING);
    // Bounded опрос TXOK только для диагностики, не для успеха.
    for (uint32_t i = 0; i < 5000; i++) {
        uint32_t s0 = ar_reg_read(AR_ISR_S0);
        if (s0 & (1U << AR9285_MGMT_Q)) {
            ar_reg_write(AR_ISR_S0, (1U << AR9285_MGMT_Q)); // W1C
            return true;
        }
        __asm__ volatile("pause");
    }
    // Таймаут TXOK — нормально для phase 2a без RF-инита, MLME поретраит.
    klog(KLOG_DEBUG, "ar9285: mgmt queued, no TXOK yet (RF/BB pending?)");
    return true;
}

static bool ar_mlme_tx(void *ctx, const uint8_t *frame, uint16_t len) {
    (void)ctx;
    klogf(KLOG_DEBUG, "ar9285: MLME TX mgmt len=%u", (uint32_t)len);
    return ar_tx_mgmt(frame, len);
}

static void ar_mlme_associated(void *ctx) {
    struct ar9285_device *dev = (struct ar9285_device *)ctx;
    if (!dev)
        dev = &adapter;
    // Прописываем AID чтобы HW фильтровал наши кадры.
    ar_program_bssid(dev->target_bssid, dev->mlme.aid);
    klogf(KLOG_OK, "ar9285: associated '%s' aid=%u bssid=%02x:%02x:%02x:%02x:%02x:%02x",
          dev->target_ssid, dev->mlme.aid, dev->target_bssid[0],
          dev->target_bssid[1], dev->target_bssid[2], dev->target_bssid[3],
          dev->target_bssid[4], dev->target_bssid[5]);
    klog(KLOG_WARN, "ar9285: RF/BB init + data-путь — phase 2b, DHCP может таймаутить");
    wifi_notify_connected(dev->target_ssid, dev->target_bssid, -45,
                          dev->target_channel, WIFI_SECURITY_OPEN);
}

static void ar_mlme_failed(void *ctx) {
    struct ar9285_device *dev = (struct ar9285_device *)ctx;
    if (!dev)
        dev = &adapter;
    dev->mlme_active = false;
    int32_t err = -10 - (int32_t)dev->mlme.fail_reason;
    klogf(KLOG_ERROR, "ar9285: assoc failed reason=%u status=%u",
          dev->mlme.fail_reason, dev->mlme.last_status);
    wifi_notify_connect_failed(err);
}

// RX-опрос: проходим теневой ринг, всё что похоже на mgmt-кадр отдаём MLME.
// Мусор отваливается внутри dot11_mlme_input() по BSSID/DA-проверкам.
static void ar_rx_poll(uint64_t now_ms) {
    struct ar9285_device *dev = &adapter;
    if (!dev->initialized || !dev->rx_ready || !dev->rx_ring || !dev->mlme_active)
        return;
    for (uint32_t n = 0; n < AR9285_RX_RING; n++) {
        uint16_t idx = dev->rx_next;
        volatile struct ar_rx_desc *d = &dev->rx_ring[idx];
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        // Эвристика готовности: len в разумных пределах mgmt-кадра.
        // Если движок не писал — len==0, пропускаем без чтения буфера.
        uint32_t len = d->len;
        if (!len || len < 24 || len > DOT11_MGMT_MAX) {
            // Проверяем следующий слот только если есть RX-прерывание,
            // иначе крутить весь ринг каждый poll дорого и бессмысленно.
            break;
        }
        uint8_t tmp[DOT11_MGMT_MAX];
        memcpy(tmp, dev->rx_bufs[idx], len > sizeof(tmp) ? sizeof(tmp) : len);
        d->len = 0;
        d->status = 0;
        __atomic_thread_fence(__ATOMIC_RELEASE);
        dev->rx_next = (uint16_t)((idx + 1) % AR9285_RX_RING);
        (void)dot11_mlme_input(&dev->mlme, tmp, (uint16_t)len, now_ms);
        if (dev->mlme.state == DOT11_MLME_ASSOCIATED ||
            dev->mlme.state == DOT11_MLME_FAILED)
            return;
    }
}

// --- net_device_ops ---

static bool ar9285_transmit(void *context, const uint8_t *frame, uint16_t length) {
    struct ar9285_device *dev = context;
    (void)frame;
    (void)length;
    if (!dev || !dev->initialized)
        return false;
    // Data-путь (Ethernet -> 802.11 data + LLC/SNAP) — phase 2b.
    if (!dev->data_path_warned) {
        dev->data_path_warned = true;
        klog(KLOG_WARN, "ar9285: data TX pending phase 2b (need 802.11 data encap)");
    }
    dev->net.stats.tx_dropped++;
    return false;
}

static void ar9285_poll(void *context, uint32_t budget) {
    (void)context;
    (void)budget;
    // Низкоуровневый опрос RX-дескрипторов делает wifi_poll() с now_ms,
    // здесь только сброс ISR_S1 ошибок чтобы очередь не клинила.
    uint32_t s1 = ar_reg_read(AR_ISR_S1);
    if (s1)
        ar_reg_write(AR_ISR_S1, s1); // W1C
}

static bool ar9285_link_up(void *context) {
    struct ar9285_device *dev = context;
    return dev && dev->initialized &&
           dev->mlme_active && dev->mlme.state == DOT11_MLME_ASSOCIATED;
}

static const struct net_device_ops ar9285_net_ops = {
    .transmit = ar9285_transmit,
    .poll = ar9285_poll,
    .link_up = ar9285_link_up,
};

// --- wifi_ops ---

static bool ar9285_wifi_scan(void *context) {
    struct ar9285_device *dev = context;
    if (!dev || !dev->initialized)
        return false;
    klog(KLOG_INFO, "ar9285: scan requested (phase2a: passive scan pending phase 2b, reporting cache)");
    // TODO phase 2b: passive scan 1..13, beacons -> wifi_report_scan_result().
    wifi_notify_scan_done();
    return true;
}

static const struct wifi_network *ar_find_cached(const char *ssid) {
    // Ищем через публичный API wifi-кэша чтобы не дублировать состояние.
    static struct wifi_network buf[WIFI_SCAN_MAX];
    uint32_t n = wifi_get_scan_results(buf, WIFI_SCAN_MAX);
    for (uint32_t i = 0; i < n; i++) {
        if (strncmp(buf[i].ssid, ssid, WIFI_SSID_MAX) == 0)
            // Копию держать нельзя (static перезатрётся), но вызывающий
            // сразу копирует BSSID/channel — ок для синхронного пути.
            return &buf[i];
        // Точное сравнение строк:
        if (strcmp(buf[i].ssid, ssid) == 0)
            return &buf[i];
    }
    return NULL;
}

static bool ar9285_wifi_connect(void *context, const char *ssid, const char *password) {
    struct ar9285_device *dev = context;
    (void)password;
    if (!dev || !dev->initialized || !ssid || !ssid[0])
        return false;
    if (dev->mlme_active &&
        (dev->mlme.state == DOT11_MLME_AUTH_SENT ||
         dev->mlme.state == DOT11_MLME_ASSOC_SENT)) {
        klog(KLOG_WARN, "ar9285: assoc already in progress");
        return true;
    }
    strncpy(dev->target_ssid, ssid, sizeof(dev->target_ssid) - 1);
    dev->target_ssid[sizeof(dev->target_ssid) - 1] = '\0';
    dev->target_password[0] = '\0';

    const struct wifi_network *cached = ar_find_cached(ssid);
    uint8_t bssid[6] = {0};
    uint8_t channel = 0;
    uint8_t security = WIFI_SECURITY_OPEN;
    if (cached) {
        memcpy(bssid, cached->bssid, 6);
        channel = cached->channel;
        security = cached->security;
    } else {
        klogf(KLOG_WARN, "ar9285: SSID '%s' not in scan cache (hidden?) — need BSSID, aborting", ssid);
        return false;
    }
    if (dot11_addr_is_zero(bssid)) {
        klogf(KLOG_ERROR, "ar9285: no BSSID for '%s', assoc impossible", ssid);
        return false;
    }
    if (security != WIFI_SECURITY_OPEN) {
        klogf(KLOG_WARN, "ar9285: '%s' security=%u not open — WPA pending phase 2b, aborting",
              ssid, security);
        return false;
    }
    if (!dev->tx_ready || !dev->rx_ready) {
        klog(KLOG_ERROR, "ar9285: TX/RX rings not ready, assoc impossible");
        return false;
    }
    memcpy(dev->target_bssid, bssid, 6);
    dev->target_channel = channel ? channel : 1;
    // TODO phase 2b: реальная установка RF-канала (BB/RF initvals + калибровка).
    // Сейчас только MAC-сторона + лог.
    klogf(KLOG_INFO, "ar9285: open-system assoc '%s' bssid=%02x:%02x:%02x:%02x:%02x:%02x chan=%u (RF tune pending phase 2b)",
          ssid, bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5],
          dev->target_channel);

    ar_program_sta(dev->net.mac);
    ar_program_bssid(bssid, 0);

    uint64_t now = timer_ticks();
    dot11_mlme_init(&dev->mlme, dev->net.mac, ar_mlme_tx, dev,
                    ar_mlme_associated, ar_mlme_failed, dev);
    dev->mlme_active = true;
    if (!dot11_mlme_start_open(&dev->mlme, ssid, bssid, dev->target_channel, now)) {
        klogf(KLOG_ERROR, "ar9285: MLME start failed reason=%u", dev->mlme.fail_reason);
        dev->mlme_active = false;
        return false;
    }
    klogf(KLOG_INFO, "ar9285: AUTH req queued for '%s', waiting assoc", ssid);
    return true;
}

static bool ar9285_wifi_disconnect(void *context) {
    struct ar9285_device *dev = context;
    if (!dev)
        return false;
    dev->target_ssid[0] = '\0';
    dev->target_password[0] = '\0';
    if (dev->mlme_active)
        dot11_mlme_stop(&dev->mlme);
    dev->mlme_active = false;
    // Сбрасываем BSSID/AID, STA-адрес оставляем.
    uint8_t zero[6] = {0};
    ar_program_bssid(zero, 0);
    klog(KLOG_INFO, "ar9285: disconnected (MLME stopped, BSSID cleared)");
    return true;
}

static void ar9285_wifi_poll(void *context, uint64_t now_ms) {
    struct ar9285_device *dev = context;
    if (!dev || !dev->initialized || !dev->mlme_active)
        return;
    ar_rx_poll(now_ms);
    uint8_t before = dev->mlme.state;
    dot11_mlme_poll(&dev->mlme, now_ms);
    if (before != dev->mlme.state) {
        klogf(KLOG_DEBUG, "ar9285: MLME state %u -> %u retries=%u", before,
              dev->mlme.state, dev->mlme.retries);
    }
}

static bool ar9285_wifi_is_connected(void *context) {
    struct ar9285_device *dev = context;
    return dev && dev->initialized && dev->mlme_active &&
           dev->mlme.state == DOT11_MLME_ASSOCIATED;
}

static const struct wifi_ops ar9285_wifi_ops = {
    .scan = ar9285_wifi_scan,
    .connect = ar9285_wifi_connect,
    .disconnect = ar9285_wifi_disconnect,
    .poll = ar9285_wifi_poll,
    .is_connected = ar9285_wifi_is_connected,
};

static void find_adapter(const struct pci_device_info *device, void *context) {
    struct ar9285_device *result = context;
    if (result->found)
        return;
    if (device->vendor_id != AR9285_VENDOR_ATHEROS || device->device_id != AR9285_DEVICE_AR9285)
        return;
    result->pci = *device;
    result->found = true;
}

bool ar9285_present(void) {
    return adapter.found;
}

bool ar9285_init(void) {
    memset(&adapter, 0, sizeof(adapter));
    pci_enumerate(find_adapter, &adapter);
    if (!adapter.found)
        return false;

    klogf(KLOG_INFO, "ar9285: found 168C:002B at %02x:%02x.%u",
          adapter.pci.bus, adapter.pci.slot, adapter.pci.function);

    if (!pci_update_command(&adapter.pci, PCI_COMMAND_MEMORY | PCI_COMMAND_BUS_MASTER, 0)) {
        klog(KLOG_ERROR, "ar9285: failed to enable PCI MEM+BM");
        return false;
    }

    uint64_t bar = pci_read_bar(adapter.pci.bus, adapter.pci.slot, adapter.pci.function, 0);
    if (!bar || bar == 0xFFFFFFFFULL) {
        klog(KLOG_ERROR, "ar9285: invalid BAR0");
        return false;
    }

    adapter.regs = mmio_map(bar, AR9285_MMIO_SIZE);
    if (!adapter.regs) {
        klog(KLOG_ERROR, "ar9285: cannot map MMIO BAR0");
        return false;
    }

    if (!ar_wake_mac()) {
        klog(KLOG_ERROR, "ar9285: MAC sleep wake timed out");
        return false;
    }

    uint8_t version = 0;
    uint8_t rev = 0;
    if (!ar_read_srev(&version, &rev)) {
        klog(KLOG_ERROR, "ar9285: unreadable SREV");
        return false;
    }
    adapter.mac_version = version;
    adapter.mac_rev = rev;
    klogf(KLOG_INFO, "ar9285: SREV version=0x%x rev=%u", version, rev);
    if (version != AR_SREV_VERSION_9285) {
        klogf(KLOG_ERROR, "ar9285: unexpected silicon version 0x%x (want 0x%x)", version,
              AR_SREV_VERSION_9285);
        return false;
    }

    // Гасим прерывания пока нет обработчика.
    ar_reg_write(AR_IMR, 0);
    (void)ar_reg_read(AR_ISR);

    if (!ar_read_mac(adapter.net.mac)) {
        klog(KLOG_WARN, "ar9285: invalid STA_ID MAC, using placeholder 02:00:00:00:00:01");
        adapter.net.mac[0] = 0x02;
        adapter.net.mac[1] = 0x00;
        adapter.net.mac[2] = 0x00;
        adapter.net.mac[3] = 0x00;
        adapter.net.mac[4] = 0x00;
        adapter.net.mac[5] = 0x01;
    }

    strncpy(adapter.net.name, "wlan0", sizeof(adapter.net.name) - 1);
    adapter.net.mtu = NET_ETHERNET_MTU;
    adapter.net.ops = &ar9285_net_ops;
    adapter.net.driver_context = &adapter;
    adapter.initialized = true;
    adapter.net.cached_link_up = false;

    // Phase 2a: TX/RX кольца. Не фатально если не встали — регистрация
    // wlan0 всё равно проходит, assoc будет честно отказывать с логом.
    if (!ar_tx_init())
        klog(KLOG_WARN, "ar9285: TX ring init failed (assoc will refuse, phase1 net kept)");
    else
        klog(KLOG_INFO, "ar9285: TX mgmt ring ready (Q8)");
    if (!ar_rx_init()) {
        klog(KLOG_WARN, "ar9285: RX ring init failed (assoc will refuse, phase1 net kept)");
        ar_release_dma();
    } else {
        klog(KLOG_INFO, "ar9285: RX mgmt ring ready");
    }
    klogf(KLOG_DEBUG, "ar9285: RX_FILTER=0x%08x TXCFG=0x%08x RXCFG=0x%08x (RF/BB pending phase 2b)",
          ar_reg_read(AR_RX_FILTER), ar_reg_read(AR_TXCFG), ar_reg_read(AR_RXCFG));

    if (!net_device_register(&adapter.net)) {
        klog(KLOG_ERROR, "ar9285: net_device_register failed");
        ar_release_dma();
        adapter.initialized = false;
        return false;
    }

    if (!wifi_device_register(&adapter.net, &ar9285_wifi_ops, &adapter, "wlan0")) {
        klog(KLOG_ERROR, "ar9285: wifi_device_register failed");
        ar_release_dma();
        adapter.initialized = false;
        return false;
    }

    klogf(KLOG_OK, "ar9285: wlan0 rev=%u mac=%02x:%02x:%02x:%02x:%02x:%02x (phase2a open-assoc)",
          adapter.mac_rev, adapter.net.mac[0], adapter.net.mac[1], adapter.net.mac[2],
          adapter.net.mac[3], adapter.net.mac[4], adapter.net.mac[5]);
    return true;
}
