#include "qemu_network_test.h"

#include "drivers/interrupts/timer.h"
#include "drivers/pci/pci.h"
#include "kernel/diagnostics/klog.h"
#include "kernel/process/scheduler.h"
#include "net/api/ping.h"
#include "net/config/dhcp.h"
#include "net/core/net_device.h"
#include "net/name/dns.h"
#include "net/network/ipv4.h"

#include <stdint.h>

#define QEMU_TEST_VENDOR 0x1B36
#define QEMU_PCI_TEST_DEVICE 0x0005
#define QEMU_TEST_DHCP_TIMEOUT_MS 30000U
#define QEMU_TEST_QUERY_TIMEOUT_MS 3000U
#define QEMU_TEST_QUERY_ATTEMPTS 2U

static const char *const public_ping_targets[] = {
    "8.8.8.8",
    "1.1.1.1",
};

static const char *const dns_targets[] = {
    "google.com",
    "cloudflare.com",
    "example.com",
};

struct qemu_test_probe {
    bool present;
};

static void find_qemu_test_device(const struct pci_device_info *device,
                                  void *context) {
    struct qemu_test_probe *probe = context;
    if (device->vendor_id == QEMU_TEST_VENDOR &&
        device->device_id == QEMU_PCI_TEST_DEVICE)
        probe->present = true;
}

bool qemu_network_test_requested(void) {
    struct qemu_test_probe probe = {0};
    pci_enumerate(find_qemu_test_device, &probe);
    return probe.present;
}

static void log_dns(const char *hostname, uint32_t address) {
    klogf(KLOG_OK,
          "[NETTEST] DNS PASS host=%s address=%u.%u.%u.%u",
          hostname,
          (address >> 24) & 255, (address >> 16) & 255,
          (address >> 8) & 255, address & 255);
}

static void fail(const char *stage, int32_t status) {
    klogf(KLOG_ERROR, "[NETTEST] RESULT FAIL stage=%s status=%d", stage, status);
}

static enum net_ping_status ping_target(const char *target, uint16_t sequence) {
    enum net_ping_status last_status = NET_PING_TIMEOUT;
    for (uint32_t attempt = 0; attempt < QEMU_TEST_QUERY_ATTEMPTS; attempt++) {
        struct net_ping_reply reply = {0};
        last_status = net_ping_target(
            target, (uint16_t)(sequence + attempt), QEMU_TEST_QUERY_TIMEOUT_MS,
            &reply);
        if (last_status == NET_PING_OK) {
            klogf(KLOG_OK,
                  "[NETTEST] PING PASS target=%s address=%u.%u.%u.%u rtt_ms=%u ttl=%u",
                  target, (reply.address >> 24) & 255,
                  (reply.address >> 16) & 255, (reply.address >> 8) & 255,
                  reply.address & 255, reply.round_trip_ms, reply.ttl);
            return NET_PING_OK;
        }
        if (attempt + 1 < QEMU_TEST_QUERY_ATTEMPTS) scheduler_sleep(100);
    }
    return last_status;
}

static bool resolve_once(struct net_device *device, const char *hostname) {
    for (uint32_t attempt = 0; attempt < QEMU_TEST_QUERY_ATTEMPTS; attempt++) {
        uint32_t address = 0;
        enum dns_result result = dns_resolve_ipv4(
            device, hostname, QEMU_TEST_QUERY_TIMEOUT_MS, &address);
        if (result == DNS_RESULT_OK) {
            log_dns(hostname, address);
            return true;
        }
        if (attempt + 1 == QEMU_TEST_QUERY_ATTEMPTS) {
            klogf(KLOG_ERROR, "[NETTEST] DNS FAIL host=%s status=%d",
                  hostname, result);
            return false;
        }
        scheduler_sleep(100);
    }
    return false;
}

void qemu_network_test_thread(void *argument) {
    (void)argument;
    klog(KLOG_INFO, "[NETTEST] START");

    uint32_t count = net_device_count();
    if (count != 1) {
        fail("device-count", (int32_t)count);
        return;
    }
    struct net_device *device = net_device_get(0);
    if (!device) {
        fail("device", -1);
        return;
    }
    klogf(KLOG_OK, "[NETTEST] DEVICE PASS interface=%s", device->name);

    uint64_t start = timer_ticks();
    struct ipv4_interface_config config = {0};
    while (timer_ticks() - start < QEMU_TEST_DHCP_TIMEOUT_MS) {
        if (dhcp_is_bound(device) && ipv4_get_config(device, &config) &&
            config.configured && dns_get_server(device))
            break;
        scheduler_sleep(50);
    }
    uint32_t dns_server = dns_get_server(device);
    if (!dhcp_is_bound(device) || !ipv4_get_config(device, &config) ||
        !config.configured || !dns_server) {
        fail("dhcp", -1);
        return;
    }
    klogf(KLOG_OK,
          "[NETTEST] DHCP PASS interface=%s address=%u.%u.%u.%u dns=%u.%u.%u.%u",
          device->name, (config.address >> 24) & 255,
          (config.address >> 16) & 255, (config.address >> 8) & 255,
          config.address & 255, (dns_server >> 24) & 255,
          (dns_server >> 16) & 255, (dns_server >> 8) & 255,
          dns_server & 255);

    uint32_t failed_checks = 0;
    enum net_ping_status gateway_status = ping_target("10.0.2.2", 1);
    bool gateway_ok = gateway_status == NET_PING_OK;
    if (!gateway_ok) {
        klogf(KLOG_ERROR,
              "[NETTEST] PING FAIL target=10.0.2.2 status=%d",
              gateway_status);
        failed_checks++;
    }

    bool dns_ok = true;
    for (uint32_t index = 0;
         index < sizeof(dns_targets) / sizeof(dns_targets[0]); index++) {
        if (!resolve_once(device, dns_targets[index])) {
            dns_ok = false;
            failed_checks++;
        }
    }

    for (uint32_t index = 0;
         index < sizeof(public_ping_targets) / sizeof(public_ping_targets[0]);
         index++) {
        const char *target = public_ping_targets[index];
        enum net_ping_status status = ping_target(
            target, (uint16_t)(index * 10 + 11));
        if (status == NET_PING_OK) continue;
        if (status == NET_PING_TIMEOUT && gateway_ok && dns_ok) {
            klogf(KLOG_WARN,
                  "[NETTEST] PING UNAVAILABLE target=%s status=%d reason=host-icmp-egress",
                  target, status);
        } else {
            klogf(KLOG_ERROR, "[NETTEST] PING FAIL target=%s status=%d",
                  target, status);
            failed_checks++;
        }
    }

    if (failed_checks)
        fail("checks", (int32_t)failed_checks);
    else
        klog(KLOG_OK, "[NETTEST] RESULT PASS");
}