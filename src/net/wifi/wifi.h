#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "../core/net_device.h"
#include "../../kernel/syscall/syscall.h"

#ifndef WIFI_SSID_MAX
#define WIFI_SSID_MAX 32
#endif
#ifndef WIFI_PASSWORD_MAX
#define WIFI_PASSWORD_MAX 64
#endif
#define WIFI_BSSID_LENGTH 6
#ifndef WIFI_SCAN_MAX
#define WIFI_SCAN_MAX 32
#endif

#ifndef WIFI_SECURITY_OPEN
#define WIFI_SECURITY_OPEN 0
#define WIFI_SECURITY_WEP 1
#define WIFI_SECURITY_WPA2 2
#define WIFI_SECURITY_WPA3 3
#define WIFI_SECURITY_WPA2_WPA3 4
#endif

#ifndef WIFI_STATE_DISCONNECTED
#define WIFI_STATE_DISCONNECTED 0
#define WIFI_STATE_SCANNING 1
#define WIFI_STATE_CONNECTING 2
#define WIFI_STATE_CONNECTED 3
#define WIFI_STATE_FAILED 4
#endif

struct wifi_network {
    char ssid[WIFI_SSID_MAX + 1];
    uint8_t bssid[WIFI_BSSID_LENGTH];
    int8_t rssi;
    uint8_t channel;
    uint8_t security;
    uint8_t reserved;
};

struct wifi_status {
    uint32_t state;
    bool connected;
    char ssid[WIFI_SSID_MAX + 1];
    uint8_t bssid[WIFI_BSSID_LENGTH];
    int8_t rssi;
    uint8_t channel;
    uint8_t security;
    int32_t last_error;
    uint32_t ip_address;
    uint8_t mac[WIFI_BSSID_LENGTH];
    char interface_name[NET_DEVICE_NAME_CAPACITY];
    uint32_t scan_count;
    uint64_t last_scan_ms;
    bool has_device;
};

struct wifi_ops {
    bool (*scan)(void *context);
    bool (*connect)(void *context, const char *ssid, const char *password);
    bool (*disconnect)(void *context);
    void (*poll)(void *context, uint64_t now_ms);
    bool (*is_connected)(void *context);
};

struct wifi_device;

void wifi_system_init(void);
bool wifi_device_register(struct net_device *net_device, const struct wifi_ops *ops, void *context, const char *name);
struct wifi_device *wifi_get_default(void);
struct net_device *wifi_get_net_device(struct wifi_device *wdev);

bool wifi_trigger_scan(void);
uint32_t wifi_get_scan_results(struct wifi_network *out, uint32_t capacity);
bool wifi_connect(const char *ssid, const char *password);
bool wifi_disconnect(void);
bool wifi_get_status(struct wifi_status *out);
void wifi_poll(uint64_t now_ms);
bool wifi_has_device(void);
const char *wifi_state_name(uint32_t state);
const char *wifi_security_name(uint8_t sec);

bool wifi_report_scan_result(const struct wifi_network *net);
void wifi_notify_scan_done(void);
void wifi_notify_connected(const char *ssid, const uint8_t bssid[6], int8_t rssi, uint8_t channel, uint8_t security);
void wifi_notify_connect_failed(int32_t error);
