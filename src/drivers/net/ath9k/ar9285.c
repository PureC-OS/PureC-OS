#include "ar9285.h"
#include "ath9k_hw.h"
#include "arch/x86_64/mmio.h"
#include "drivers/interrupts/timer.h"
#include "drivers/pci/pci.h"
#include "kernel/core/system_info.h"
#include "kernel/diagnostics/klog.h"
#include "lib/string.h"
#include "mm/pmm.h"
#include "net/802.11/include/802.h"
#include "net/802.11/b/b_mgmt.h"
#include "net/core/net_device.h"
#include "net/wifi/wifi.h"
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#define AR9285_VENDOR_ATHEROS 0x168C
#define AR9285_DEVICE_AR9285 0x002B
#define AR9285_MMIO_SIZE 0x10000U
#define AR9285_SCAN_DWELL_MS 110U
#define AR9285_SCAN_FIRST_CHANNEL 1
#define AR9285_SCAN_LAST_CHANNEL 11
#define AR9285_ASSOC_TIMEOUT_MS 10000ULL
#define AR9285_NOISE_FLOOR_DBM (-95)
#define AR9285_RX_BUDGET 16

struct ar9285_device {
    struct ath_hw hw;
    struct pci_device_info pci;
    struct net_device net;
    bool found;
    bool initialized;
    bool radio_ready;

    struct dot11_mlme mlme;
    bool mlme_active;
    char target_ssid[WIFI_SSID_MAX + 1];
    uint8_t target_bssid[6];
    uint8_t target_channel;
    uint8_t cur_channel;
    uint64_t connect_start_ms;
    uint8_t data_rate;
    uint16_t tx_seq;
    int8_t last_rssi;

    bool scan_active;
    uint8_t scan_channel;
    uint64_t scan_channel_start_ms;
    uint32_t scan_hits;

    uint32_t data_rx;
    uint32_t data_rx_dropped;
    uint32_t mgmt_rx;
};

static struct ar9285_device adapter;

void ath9k_udelay(uint32_t us)
{
    uint64_t freq = system_info_tsc_frequency_hz();
    uint64_t start, ticks;
    uint32_t lo, hi;

    if (!freq)
        freq = 2000000000ULL;
    ticks = freq / 1000000ULL * us;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    start = ((uint64_t)hi << 32) | lo;
    for (;;) {
        uint64_t now;

        __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
        now = ((uint64_t)hi << 32) | lo;
        if (now - start >= ticks)
            break;
        __asm__ volatile("pause");
    }
}

void ath9k_log(int level, const char *fmt, ...)
{
    va_list ap;
    enum klog_level lvl = KLOG_INFO;

    if (level == ATH9K_LOG_ERR)
        lvl = KLOG_ERROR;
    else if (level == ATH9K_LOG_WARN)
        lvl = KLOG_WARN;
    else if (level == ATH9K_LOG_DBG)
        lvl = KLOG_DEBUG;
    va_start(ap, fmt);
    klog_vf(lvl, fmt, ap);
    va_end(ap);
}

uint64_t ath9k_dma_alloc_page(void **virt)
{
    uint64_t held[16];
    uint32_t held_count = 0;
    uint64_t result = 0;

    for (uint32_t attempt = 0; attempt < 16; attempt++) {
        uint64_t page = pmm_allocate_page();

        if (!page)
            break;
        if (page + 4096 <= 0x100000000ULL) {
            result = page;
            break;
        }
        held[held_count++] = page;
    }
    for (uint32_t i = 0; i < held_count; i++)
        pmm_free_page(held[i]);
    if (result && virt)
        *virt = pmm_physical_to_virtual(result);
    return result;
}

void ath9k_dma_free_page(uint64_t phys)
{
    if (phys)
        pmm_free_page(phys);
}

static bool addr_is_multicast(const uint8_t *a)
{
    return (a[0] & 1) != 0;
}

static int8_t rssi_to_dbm(int8_t raw)
{
    int v = AR9285_NOISE_FLOOR_DBM + raw;

    if (v > -10)
        v = -10;
    if (v < -127)
        v = -127;
    return (int8_t)v;
}

static bool hw_send(struct ar9285_device *dev, const uint8_t *frame, uint16_t len,
                    uint8_t rate, bool no_ack)
{
    if (!dev->radio_ready)
        return false;
    return ath9k_hw_tx_frame(&dev->hw, frame, len, rate, no_ack);
}

static bool ar_mlme_tx(void *ctx, const uint8_t *frame, uint16_t len)
{
    struct ar9285_device *dev = ctx;

    if (!dev)
        dev = &adapter;
    return hw_send(dev, frame, len, ATH9K_HWRATE_1M, false);
}

static uint32_t rx_filter_value(void)
{
    return ATH9K_RX_FILTER_UCAST | ATH9K_RX_FILTER_BCAST | ATH9K_RX_FILTER_MCAST |
           ATH9K_RX_FILTER_BEACON;
}

static bool radio_tune(struct ar9285_device *dev, uint8_t channel, const uint8_t *bssid,
                       uint16_t aid)
{
    static const uint8_t zero[6] = {0};
    uint16_t freq = ath9k_channel_to_freq(channel);

    if (!freq)
        return false;
    ath9k_hw_rx_stop(&dev->hw);
    if (ath9k_hw_reset(&dev->hw, freq) != 0) {
        klogf(KLOG_ERROR, "ar9285: hw reset for channel %u failed", channel);
        dev->radio_ready = false;
        return false;
    }
    ath9k_hw_set_macaddr(&dev->hw, dev->net.mac);
    ath9k_hw_set_bssid(&dev->hw, bssid ? bssid : zero, aid);
    ath9k_hw_set_rx_filter(&dev->hw, rx_filter_value());
    if (ath9k_hw_rx_start(&dev->hw) != 0) {
        klog(KLOG_ERROR, "ar9285: RX ring start failed");
        dev->radio_ready = false;
        return false;
    }
    dev->cur_channel = channel;
    dev->radio_ready = true;
    return true;
}

static void ar_mlme_associated(void *ctx)
{
    struct ar9285_device *dev = ctx;

    if (!dev)
        dev = &adapter;
    ath9k_hw_set_bssid(&dev->hw, dev->target_bssid, dev->mlme.aid);

    dev->data_rate = ATH9K_HWRATE_1M;
    for (uint8_t i = 0; i < dev->mlme.rate_count; i++)
        if ((dev->mlme.rates[i] & 0x7F) == 0x16)
            dev->data_rate = ATH9K_HWRATE_11M;

    klogf(KLOG_OK, "ar9285: associated '%s' aid=%u bssid=%02x:%02x:%02x:%02x:%02x:%02x rssi=%d",
          dev->target_ssid, dev->mlme.aid, dev->target_bssid[0], dev->target_bssid[1],
          dev->target_bssid[2], dev->target_bssid[3], dev->target_bssid[4],
          dev->target_bssid[5], dev->last_rssi);
    wifi_notify_connected(dev->target_ssid, dev->target_bssid, dev->last_rssi,
                          dev->target_channel, WIFI_SECURITY_OPEN);
}

static void ar_mlme_failed(void *ctx)
{
    struct ar9285_device *dev = ctx;
    int32_t err;

    if (!dev)
        dev = &adapter;
    dev->mlme_active = false;
    err = -10 - (int32_t)dev->mlme.fail_reason;
    klogf(KLOG_ERROR, "ar9285: assoc failed %s status=%u aid=%u (err=%d)",
          dot11_mlme_fail_name(dev->mlme.fail_reason), dev->mlme.last_status,
          dev->mlme.aid, err);
    wifi_notify_connect_failed(err);
}

static void scan_report(struct ar9285_device *dev, const struct dot11_bss *bss, int8_t rssi)
{
    struct wifi_network net;

    if (!bss->ssid_len || !bss->ssid[0])
        return;
    memset(&net, 0, sizeof(net));
    memcpy(net.ssid, bss->ssid, sizeof(net.ssid) - 1);
    memcpy(net.bssid, bss->bssid, 6);
    net.channel = bss->channel ? bss->channel : dev->cur_channel;
    net.rssi = rssi;
    net.security = ((bss->capability & DOT11_CAP_PRIVACY) || bss->has_rsn)
                       ? WIFI_SECURITY_WPA2 : WIFI_SECURITY_OPEN;
    if (wifi_report_scan_result(&net))
        dev->scan_hits++;
}

static void handle_mgmt(struct ar9285_device *dev, const uint8_t *frame, uint16_t len,
                        int8_t rssi, uint64_t now_ms)
{
    dev->mgmt_rx++;
    if (dev->scan_active) {
        struct dot11_bss bss;

        if (dot11_parse_bss(frame, len, &bss))
            scan_report(dev, &bss, rssi);
        return;
    }
    if (!dev->mlme_active)
        return;
    if (dot11_mlme_input(&dev->mlme, frame, len, now_ms)) {
        struct dot11_bss bss;

        if (dot11_parse_bss(frame, len, &bss) &&
            memcmp(bss.bssid, dev->target_bssid, 6) == 0)
            {
                dev->last_rssi = rssi;
                wifi_update_rssi(rssi);
            }
    }
}

static void handle_data(struct ar9285_device *dev, const uint8_t *frame, uint16_t len,
                        int8_t rssi)
{
    uint8_t eth[NET_ETHERNET_MAX_FRAME_SIZE];
    uint16_t eth_len = 0;

    dev->data_rx++;
    if (dev->mlme_active && dev->mlme.state == DOT11_MLME_ASSOCIATED)
        eth_len = dot11_data_to_ethernet(frame, len, dev->target_bssid, eth, sizeof(eth));
    if (eth_len) {
        dev->last_rssi = rssi;
        wifi_update_rssi(rssi);
        if (net_device_receive(&dev->net, eth, eth_len))
            return;
    }
    dev->data_rx_dropped++;
    dev->net.stats.rx_dropped++;
}

static void rx_poll(struct ar9285_device *dev, uint64_t now_ms)
{
    struct ath9k_rx_frame fr;

    for (uint32_t n = 0; n < AR9285_RX_BUDGET; n++) {
        uint16_t fc;
        uint8_t ftype;

        if (!dev->radio_ready || !ath9k_hw_rx_next(&dev->hw, &fr))
            return;
        if (fr.len < 24)
            continue;
        fc = (uint16_t)fr.data[0] | ((uint16_t)fr.data[1] << 8);
        ftype = (uint8_t)DOT11_FC_TYPE(fc);
        if (ftype == DOT11_FTYPE_DATA)
            handle_data(dev, fr.data, fr.len, rssi_to_dbm(fr.rssi));
        else if (ftype == DOT11_FTYPE_MGMT)
            handle_mgmt(dev, fr.data, fr.len, rssi_to_dbm(fr.rssi), now_ms);
    }
}

static bool ar9285_transmit(void *context, const uint8_t *frame, uint16_t length)
{
    struct ar9285_device *dev = context;
    uint8_t out[ATH9K_MAX_TX_LEN];
    uint16_t total;

    if (!dev || !dev->initialized || !frame || length < 14)
        return false;
    if (!dev->mlme_active || dev->mlme.state != DOT11_MLME_ASSOCIATED) {
        dev->net.stats.tx_dropped++;
        return false;
    }

    total = dot11_ethernet_to_data(frame, length, dev->target_bssid,
                                   (uint16_t)(dev->tx_seq << 4), out, sizeof(out));
    if (!total) {
        dev->net.stats.tx_dropped++;
        return false;
    }
    dev->tx_seq = (uint16_t)((dev->tx_seq + 1) & 0x0FFF);

    if (!hw_send(dev, out, total, dev->data_rate, addr_is_multicast(frame))) {
        dev->net.stats.tx_dropped++;
        return false;
    }
    return true;
}

static void ar9285_poll(void *context, uint32_t budget)
{
    (void)budget;
    if (context)
        ath9k_hw_tx_reap(&((struct ar9285_device *)context)->hw);
}

static bool ar9285_link_up(void *context)
{
    struct ar9285_device *dev = context;

    return dev && dev->initialized && dev->mlme_active &&
           dev->mlme.state == DOT11_MLME_ASSOCIATED;
}

static const struct net_device_ops ar9285_net_ops = {
    .transmit = ar9285_transmit,
    .poll = ar9285_poll,
    .link_up = ar9285_link_up,
};

static void scan_send_probe(struct ar9285_device *dev)
{
    static const uint8_t b_rates[] = {0x82, 0x84, 0x8B, 0x96};
    uint8_t probe[DOT11_MGMT_MAX];
    uint16_t plen = dot11b_build_probe_req(dev->net.mac, NULL, 0, b_rates,
                                           sizeof(b_rates), probe, sizeof(probe));

    if (plen)
        (void)hw_send(dev, probe, plen, ATH9K_HWRATE_1M, true);
}

static void scan_finish(struct ar9285_device *dev)
{
    dev->scan_active = false;
    klogf(KLOG_INFO, "ar9285: scan finished, %u beacons/probe responses", dev->scan_hits);
    wifi_notify_scan_done();
}

static bool scan_step(struct ar9285_device *dev, uint64_t now_ms)
{
    if (!radio_tune(dev, dev->scan_channel, NULL, 0)) {
        scan_finish(dev);
        return false;
    }
    dev->scan_channel_start_ms = now_ms;
    scan_send_probe(dev);
    return true;
}

static bool ar9285_wifi_scan(void *context)
{
    struct ar9285_device *dev = context;

    if (!dev || !dev->initialized || !dev->radio_ready)
        return false;
    if (dev->mlme_active) {
        klog(KLOG_INFO, "ar9285: scan deferred while associating or associated");
        return false;
    }
    if (dev->scan_active)
        return true;
    dev->scan_active = true;
    dev->scan_hits = 0;
    dev->scan_channel = AR9285_SCAN_FIRST_CHANNEL;
    klog(KLOG_INFO, "ar9285: scan started (channels 1-11)");
    return scan_step(dev, timer_ticks());
}

static const struct wifi_network *find_cached(const char *ssid)
{
    static struct wifi_network buf[WIFI_SCAN_MAX];
    uint32_t n = wifi_get_scan_results(buf, WIFI_SCAN_MAX);

    for (uint32_t i = 0; i < n; i++)
        if (strcmp(buf[i].ssid, ssid) == 0)
            return &buf[i];
    return NULL;
}

static bool ar9285_wifi_connect(void *context, const char *ssid, const char *password)
{
    struct ar9285_device *dev = context;
    const struct wifi_network *cached;
    uint64_t now;

    (void)password;
    if (!dev || !dev->initialized || !ssid || !ssid[0])
        return false;

    dev->scan_active = false;
    if (dev->mlme_active) {
        dot11_mlme_stop(&dev->mlme);
        dev->mlme_active = false;
    }

    cached = find_cached(ssid);
    if (!cached) {
        klogf(KLOG_WARN, "ar9285: SSID '%s' not in scan cache, need BSSID", ssid);
        return false;
    }
    if (cached->security != WIFI_SECURITY_OPEN) {
        klogf(KLOG_WARN, "ar9285: '%s' is %s, only open networks are supported", ssid,
              wifi_security_name(cached->security));
        return false;
    }

    strncpy(dev->target_ssid, ssid, sizeof(dev->target_ssid) - 1);
    dev->target_ssid[sizeof(dev->target_ssid) - 1] = '\0';
    memcpy(dev->target_bssid, cached->bssid, 6);
    dev->target_channel = cached->channel ? cached->channel : 1;
    dev->last_rssi = cached->rssi;
    wifi_update_rssi(cached->rssi);

    if (!radio_tune(dev, dev->target_channel, dev->target_bssid, 0))
        return false;

    now = timer_ticks();
    dot11_mlme_init(&dev->mlme, dev->net.mac, ar_mlme_tx, dev, ar_mlme_associated,
                    ar_mlme_failed, dev);
    dev->mlme_active = true;
    dev->connect_start_ms = now;
    if (!dot11_mlme_start_open(&dev->mlme, ssid, dev->target_bssid, dev->target_channel, now)) {
        klogf(KLOG_ERROR, "ar9285: MLME start failed reason=%u", dev->mlme.fail_reason);
        dev->mlme_active = false;
        return false;
    }
    klogf(KLOG_INFO, "ar9285: auth to '%s' ch%u started", ssid, dev->target_channel);
    return true;
}

static bool ar9285_wifi_disconnect(void *context)
{
    static const uint8_t zero[6] = {0};
    struct ar9285_device *dev = context;

    if (!dev)
        return false;
    dev->scan_active = false;
    dev->target_ssid[0] = '\0';
    if (dev->mlme_active)
        dot11_mlme_stop(&dev->mlme);
    dev->mlme_active = false;
    if (dev->radio_ready)
        ath9k_hw_set_bssid(&dev->hw, zero, 0);
    klog(KLOG_INFO, "ar9285: disconnected");
    return true;
}

static void ar9285_wifi_poll(void *context, uint64_t now_ms)
{
    struct ar9285_device *dev = context;

    if (!dev || !dev->initialized || !dev->radio_ready)
        return;

    rx_poll(dev, now_ms);
    ath9k_hw_tx_reap(&dev->hw);

    if (dev->scan_active && now_ms - dev->scan_channel_start_ms >= AR9285_SCAN_DWELL_MS) {
        dev->scan_channel++;
        if (dev->scan_channel > AR9285_SCAN_LAST_CHANNEL)
            scan_finish(dev);
        else
            (void)scan_step(dev, now_ms);
        return;
    }

    if (!dev->mlme_active)
        return;

    {
        uint8_t before = dev->mlme.state;

        dot11_mlme_poll(&dev->mlme, now_ms);
        if (before != dev->mlme.state)
            klogf(KLOG_DEBUG, "ar9285: MLME %s -> %s retries=%u",
                  dot11_mlme_state_name(before), dot11_mlme_state_name(dev->mlme.state),
                  dev->mlme.retries);
    }

    if (dev->mlme_active &&
        (dev->mlme.state == DOT11_MLME_AUTH_SENT || dev->mlme.state == DOT11_MLME_AUTH_OK ||
         dev->mlme.state == DOT11_MLME_ASSOC_SENT) &&
        now_ms - dev->connect_start_ms >= AR9285_ASSOC_TIMEOUT_MS) {
        klogf(KLOG_ERROR, "ar9285: assoc timeout state=%s tx_ok=%u tx_fail=%u mgmt_rx=%u",
              dot11_mlme_state_name(dev->mlme.state), dev->hw.tx_ok, dev->hw.tx_fail,
              dev->mgmt_rx);
        dot11_mlme_stop(&dev->mlme);
        dev->mlme_active = false;
        wifi_notify_connect_failed(-11);
    }
}

static bool ar9285_wifi_is_connected(void *context)
{
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

static void find_adapter(const struct pci_device_info *device, void *context)
{
    struct ar9285_device *result = context;

    if (result->found)
        return;
    if (device->vendor_id != AR9285_VENDOR_ATHEROS || device->device_id != AR9285_DEVICE_AR9285)
        return;
    result->pci = *device;
    result->found = true;
}

bool ar9285_present(void)
{
    return adapter.found;
}

bool ar9285_init(void)
{
    struct pci_device_info pci;
    volatile uint8_t *regs;
    uint64_t bar;
    uint32_t cache_line;

    memset(&adapter, 0, sizeof(adapter));
    pci_enumerate(find_adapter, &adapter);
    if (!adapter.found)
        return false;
    pci = adapter.pci;

    klogf(KLOG_INFO, "ar9285: found 168C:002B at %02x:%02x.%u", pci.bus, pci.slot, pci.function);

    if (!pci_update_command(&pci, PCI_COMMAND_MEMORY | PCI_COMMAND_BUS_MASTER, 0)) {
        klog(KLOG_ERROR, "ar9285: failed to enable PCI MEM+BM");
        return false;
    }
    cache_line = pci_read_config32(pci.bus, pci.slot, pci.function, 0x0C) & 0xFF;
    if (!cache_line)
        pci_write_config8(pci.bus, pci.slot, pci.function, 0x0C, 16);
    pci_write_config8(pci.bus, pci.slot, pci.function, 0x41, 0);

    bar = pci_read_bar(pci.bus, pci.slot, pci.function, 0);
    if (!bar || bar == 0xFFFFFFFFULL) {
        klog(KLOG_ERROR, "ar9285: invalid BAR0");
        return false;
    }
    regs = mmio_map(bar, AR9285_MMIO_SIZE);
    if (!regs) {
        klog(KLOG_ERROR, "ar9285: cannot map MMIO BAR0");
        return false;
    }

    if (ath9k_hw_init(&adapter.hw, regs) != 0) {
        klog(KLOG_ERROR, "ar9285: hardware init failed");
        return false;
    }
    memcpy(adapter.net.mac, adapter.hw.mac_addr, 6);

    if (ath9k_hw_tx_init(&adapter.hw) != 0) {
        klog(KLOG_ERROR, "ar9285: TX ring allocation failed");
        return false;
    }

    strncpy(adapter.net.name, "wlan0", sizeof(adapter.net.name) - 1);
    adapter.net.mtu = NET_ETHERNET_MTU;
    adapter.net.ops = &ar9285_net_ops;
    adapter.net.driver_context = &adapter;
    adapter.net.cached_link_up = false;
    adapter.initialized = true;
    adapter.data_rate = ATH9K_HWRATE_1M;

    if (!radio_tune(&adapter, 6, NULL, 0))
        klog(KLOG_WARN, "ar9285: radio bring-up failed, scan will be unavailable");

    if (!net_device_register(&adapter.net)) {
        klog(KLOG_ERROR, "ar9285: net_device_register failed");
        ath9k_hw_rx_release(&adapter.hw);
        ath9k_hw_tx_release(&adapter.hw);
        adapter.initialized = false;
        return false;
    }
    if (!wifi_device_register(&adapter.net, &ar9285_wifi_ops, &adapter, "wlan0")) {
        klog(KLOG_ERROR, "ar9285: wifi_device_register failed");
        ath9k_hw_rx_release(&adapter.hw);
        ath9k_hw_tx_release(&adapter.hw);
        adapter.initialized = false;
        return false;
    }

    klogf(KLOG_OK, "ar9285: wlan0 rev=%u mac=%02x:%02x:%02x:%02x:%02x:%02x radio=%s",
          adapter.hw.mac_rev, adapter.net.mac[0], adapter.net.mac[1], adapter.net.mac[2],
          adapter.net.mac[3], adapter.net.mac[4], adapter.net.mac[5],
          adapter.radio_ready ? "up" : "down");
    return true;
}
