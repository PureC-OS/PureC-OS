#include "ar9285.h"
#include "arch/x86_64/mmio.h"
#include "drivers/pci/pci.h"
#include "kernel/diagnostics/klog.h"
#include "lib/string.h"
#include "net/core/net_device.h"
#include "net/wifi/wifi.h"
#include <stddef.h>
#include <stdint.h>

#define AR9285_VENDOR_ATHEROS 0x168C
#define AR9285_DEVICE_AR9285 0x002B
#define AR9285_MMIO_SIZE 0x10000U
#define AR9285_POLL_TIMEOUT 1000000U
#define AR_CR 0x0008
#define AR_CR_RXE 0x00000004
#define AR_CR_RXD 0x00000020
#define AR_CFG 0x0014
#define AR_CFG_PHOK 0x00000100
#define AR_ISR 0x0080
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
#define AR_SREV_VERSION_9285 0x0C
#define AR_STA_ID0 0x8000
#define AR_STA_ID1 0x8004
#define AR_STA_ID1_SADH_MASK 0x0000FFFF

struct ar9285_device {
    volatile uint8_t *regs;
    struct pci_device_info pci;
    struct net_device net;
    uint8_t mac_version;
    uint8_t mac_rev;
    char target_ssid[WIFI_SSID_MAX + 1];
    char target_password[WIFI_PASSWORD_MAX + 1];
    bool found;
    bool initialized;
};

static struct ar9285_device adapter;

static uint32_t ar_reg_read(uint32_t offset) {
    return *(volatile uint32_t *)(adapter.regs + offset);
}

static void ar_reg_write(uint32_t offset, uint32_t value) {
    *(volatile uint32_t *)(adapter.regs + offset) = value;
}

static bool ar_wake_mac(void) {
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

// --- net_device_ops (заглушки phase 1) ---

static bool ar9285_transmit(void *context, const uint8_t *frame, uint16_t length) {
    struct ar9285_device *dev = context;
    (void)frame;
    (void)length;
    if (!dev || !dev->initialized)
        return false;
    // TODO phase 2: QCU/DCU + TX-дескрипторы.
    dev->net.stats.tx_dropped++;
    return false;
}

static void ar9285_poll(void *context, uint32_t budget) {
    (void)context;
    (void)budget;
    // TODO phase 2: RX-дескрипторы + net_device_receive().
}

static bool ar9285_link_up(void *context) {
    struct ar9285_device *dev = context;
    // Линк = association, которой пока нет.
    return dev && dev->initialized && false;
}

static const struct net_device_ops ar9285_net_ops = {
    .transmit = ar9285_transmit,
    .poll = ar9285_poll,
    .link_up = ar9285_link_up,
};

// --- wifi_ops (минимальные, честные) ---

static bool ar9285_wifi_scan(void *context) {
    struct ar9285_device *dev = context;
    if (!dev || !dev->initialized)
        return false;
    klog(KLOG_INFO, "ar9285: scan requested (phase1: no MLME yet, reporting empty)");
    // TODO phase 2: passive scan по каналам 1..13 + beacons в wifi_report_scan_result().
    wifi_notify_scan_done();
    return true;
}

static bool ar9285_wifi_connect(void *context, const char *ssid, const char *password) {
    struct ar9285_device *dev = context;
    if (!dev || !dev->initialized || !ssid || !ssid[0])
        return false;
    strncpy(dev->target_ssid, ssid, sizeof(dev->target_ssid) - 1);
    dev->target_ssid[sizeof(dev->target_ssid) - 1] = '\0';
    if (password) {
        strncpy(dev->target_password, password, sizeof(dev->target_password) - 1);
        dev->target_password[sizeof(dev->target_password) - 1] = '\0';
    } else {
        dev->target_password[0] = '\0';
    }
    klogf(KLOG_WARN, "ar9285: connect to '%s' deferred (phase1: no assoc/WPA yet)", dev->target_ssid);
    // Возвращаем true чтобы wifi-менеджер перешёл в CONNECTING,
    // но is_connected() останется false пока нет phase 2.
    // TODO phase 2: open-system assoc, затем WPA2 handshake.
    return true;
}

static bool ar9285_wifi_disconnect(void *context) {
    struct ar9285_device *dev = context;
    if (!dev)
        return false;
    dev->target_ssid[0] = '\0';
    dev->target_password[0] = '\0';
    klog(KLOG_INFO, "ar9285: disconnect (phase1: nothing to tear down)");
    return true;
}

static void ar9285_wifi_poll(void *context, uint64_t now_ms) {
    (void)context;
    (void)now_ms;
    // TODO phase 2: дотягивать scan/assoc-таймауты, RSSI.
}

static bool ar9285_wifi_is_connected(void *context) {
    (void)context;
    return false;
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

    if (!net_device_register(&adapter.net)) {
        klog(KLOG_ERROR, "ar9285: net_device_register failed");
        adapter.initialized = false;
        return false;
    }

    if (!wifi_device_register(&adapter.net, &ar9285_wifi_ops, &adapter, "wlan0")) {
        klog(KLOG_ERROR, "ar9285: wifi_device_register failed");
        adapter.initialized = false;
        return false;
    }

    klogf(KLOG_OK, "ar9285: wlan0 rev=%u mac=%02x:%02x:%02x:%02x:%02x:%02x (phase1 bring-up)",
          adapter.mac_rev, adapter.net.mac[0], adapter.net.mac[1], adapter.net.mac[2],
          adapter.net.mac[3], adapter.net.mac[4], adapter.net.mac[5]);
    return true;
}
