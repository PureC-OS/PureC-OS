#include "wifi.h"
#include "../../kernel/diagnostics/klog.h"
#include "../../drivers/interrupts/timer.h"
#include "../../lib/string.h"
#include "../../net/network/ipv4.h"
#include "../../net/name/dns.h"
#include "../../net/config/dhcp.h"
#include <stddef.h>

#define WIFI_SCAN_INTERVAL_MS 5000ULL
#define WIFI_SCAN_TIMEOUT_MS 4000ULL

#define WIFI_MAX_DEVICES 2

struct wifi_slot {
    bool used;
    struct net_device *net;
    const struct wifi_ops *ops;
    void *context;
    char name[NET_DEVICE_NAME_CAPACITY];
};

struct wifi_manager {
    struct wifi_slot slots[WIFI_MAX_DEVICES];
    struct wifi_slot *active;
    struct wifi_network cache[WIFI_SCAN_MAX];
    uint32_t cache_count;
    uint32_t state;
    char connected_ssid[WIFI_SSID_MAX+1];
    char target_ssid[WIFI_SSID_MAX+1];
    char target_password[WIFI_PASSWORD_MAX+1];
    uint8_t target_bssid[6];
    uint8_t target_channel;
    uint8_t target_security;
    int8_t connected_rssi;
    uint64_t last_scan_ms;
    uint64_t scan_start_ms;
    uint64_t connect_start_ms;
    bool scanning;
    bool connecting;
    int32_t last_error;
};

static struct wifi_manager mgr;
static bool initialized;

void wifi_system_init(void){
    memset(&mgr,0,sizeof(mgr));
    mgr.state = WIFI_STATE_DISCONNECTED;
    initialized = true;
    klog(KLOG_INFO, "wifi: subsystem initialized");
}

bool wifi_device_register(struct net_device *net_device, const struct wifi_ops *ops, void *context, const char *name){
    if(!net_device || !ops || !name) return false;
    for(uint32_t i=0;i<WIFI_MAX_DEVICES;i++){
        if(mgr.slots[i].used) continue;
        mgr.slots[i].used = true;
        mgr.slots[i].net = net_device;
        mgr.slots[i].ops = ops;
        mgr.slots[i].context = context;
        strncpy(mgr.slots[i].name, name, sizeof(mgr.slots[i].name)-1);
        mgr.slots[i].name[sizeof(mgr.slots[i].name)-1]='\0';
        if(!mgr.active) mgr.active = &mgr.slots[i];
        klogf(KLOG_OK, "wifi: device registered %s mac=%02x:%02x:%02x:%02x:%02x:%02x",
            name, net_device->mac[0], net_device->mac[1], net_device->mac[2],
            net_device->mac[3], net_device->mac[4], net_device->mac[5]);
        return true;
    }
    return false;
}

struct wifi_device *wifi_get_default(void){ return (struct wifi_device*)mgr.active; }
struct net_device *wifi_get_net_device(struct wifi_device *wdev){
    struct wifi_slot *s=(struct wifi_slot*)wdev;
    return s ? s->net : NULL;
}
bool wifi_has_device(void){ return mgr.active != NULL; }

bool wifi_trigger_scan(void){
    if(!initialized) return false;
    if(!mgr.active){
        klog(KLOG_WARN, "wifi: scan requested but no device");
        return false;
    }
    if(mgr.scanning){
        return false;
    }
    if(mgr.connecting){
        klog(KLOG_INFO, "wifi: scan deferred while connecting");
        return false;
    }
    mgr.scanning = true;
    mgr.scan_start_ms = timer_ticks();
    mgr.state = WIFI_STATE_SCANNING;
    mgr.cache_count = 0;
    klog(KLOG_INFO, "wifi: scanning started (driver will report beacons)");
    if(mgr.active->ops && mgr.active->ops->scan){
        bool ok = mgr.active->ops->scan(mgr.active->context);
        if(!ok){
            klog(KLOG_WARN, "wifi: driver scan failed to start");
            mgr.scanning = false;
            mgr.state = WIFI_STATE_DISCONNECTED;
            return false;
        }
    }
    return true;
}

uint32_t wifi_get_scan_results(struct wifi_network *out, uint32_t capacity){
    if(!out || !capacity) return mgr.cache_count;
    uint32_t copy = mgr.cache_count < capacity ? mgr.cache_count : capacity;
    for(uint32_t i=0;i<copy;i++) out[i]=mgr.cache[i];
    return copy;
}

static struct wifi_network *find_network(const char *ssid){
    for(uint32_t i=0;i<mgr.cache_count;i++){
        if(strcmp(mgr.cache[i].ssid, ssid)==0) return &mgr.cache[i];
    }
    return NULL;
}

bool wifi_report_scan_result(const struct wifi_network *net){
    if(!net || !net->ssid[0]) return false;

    for(uint32_t i=0;i<mgr.cache_count;i++){
        if(memcmp(mgr.cache[i].bssid, net->bssid, 6)==0){
            mgr.cache[i]=*net;
            return true;
        }
    }
    if(mgr.cache_count >= WIFI_SCAN_MAX) return false;
    mgr.cache[mgr.cache_count++] = *net;
    return true;
}

void wifi_notify_scan_done(void){
    mgr.last_scan_ms = timer_ticks();
    mgr.scanning = false;
    if(mgr.state == WIFI_STATE_SCANNING && !mgr.connecting){
        mgr.state = WIFI_STATE_DISCONNECTED;
    }

    for(uint32_t i=0;i<mgr.cache_count;i++){
        for(uint32_t j=i+1;j<mgr.cache_count;j++){
            if(mgr.cache[j].rssi > mgr.cache[i].rssi){
                struct wifi_network tmp=mgr.cache[i];
                mgr.cache[i]=mgr.cache[j];
                mgr.cache[j]=tmp;
            }
        }
    }
    klogf(KLOG_INFO, "wifi: scan complete, %u networks found", mgr.cache_count);
    for(uint32_t i=0;i<mgr.cache_count;i++){
        klogf(KLOG_DEBUG, "wifi:  [%u] %s rssi=%d chan=%u sec=%s bssid=%02x:%02x:%02x:%02x:%02x:%02x",
            i, mgr.cache[i].ssid, mgr.cache[i].rssi, mgr.cache[i].channel,
            wifi_security_name(mgr.cache[i].security),
            mgr.cache[i].bssid[0], mgr.cache[i].bssid[1], mgr.cache[i].bssid[2],
            mgr.cache[i].bssid[3], mgr.cache[i].bssid[4], mgr.cache[i].bssid[5]);
    }
}

void wifi_notify_connected(const char *ssid, const uint8_t bssid[6], int8_t rssi, uint8_t channel, uint8_t security){
    if(!ssid || !mgr.active) return;
    strncpy(mgr.connected_ssid, ssid, sizeof(mgr.connected_ssid)-1);
    mgr.connected_ssid[sizeof(mgr.connected_ssid)-1]='\0';
    if(bssid) memcpy(mgr.target_bssid, bssid, 6);
    mgr.connected_rssi = rssi;
    mgr.target_channel = channel;
    mgr.target_security = security;
    mgr.state = WIFI_STATE_CONNECTED;
    mgr.connecting = false;
    mgr.last_error = 0;
    if(mgr.active->net){
        mgr.active->net->cached_link_up = true;

        (void)dhcp_start(mgr.active->net);
        klogf(KLOG_OK, "wifi: associated to '%s' bssid=%02x:%02x:%02x:%02x:%02x:%02x rssi=%d - starting DHCP",
            ssid, bssid?bssid[0]:0,bssid?bssid[1]:0,bssid?bssid[2]:0,bssid?bssid[3]:0,bssid?bssid[4]:0,bssid?bssid[5]:0, rssi);
        klog(KLOG_INFO, "wifi: DHCP will configure IP via net stack (no fake addresses)");
    }
}

void wifi_notify_connect_failed(int32_t error){
    mgr.state = WIFI_STATE_FAILED;
    mgr.last_error = error;
    mgr.connecting = false;
    if(mgr.active && mgr.active->net) mgr.active->net->cached_link_up = false;
    klogf(KLOG_ERROR, "wifi: association failed for '%s' error=%d", mgr.target_ssid, error);
}

bool wifi_connect(const char *ssid, const char *password){
    if(!ssid || !ssid[0] || strlen(ssid) > WIFI_SSID_MAX) return false;
    if(!mgr.active){
        mgr.last_error = -1;
        klog(KLOG_ERROR, "wifi: connect failed - no device");
        return false;
    }
    if(mgr.connecting){
        klog(KLOG_WARN, "wifi: already connecting");
        return false;
    }
    struct wifi_network *net = find_network(ssid);
    uint8_t security = WIFI_SECURITY_WPA2;
    uint8_t channel = 0;
    uint8_t bssid[6]={0};
    if(net){
        security = net->security;
        channel = net->channel;
        memcpy(bssid, net->bssid, 6);
    } else {
        klogf(KLOG_WARN, "wifi: connecting to SSID '%s' not in cache (hidden?)", ssid);
        security = WIFI_SECURITY_WPA2;
    }
    if(security != WIFI_SECURITY_OPEN){
        if(!password) password="";
        size_t len = strlen(password);
        if(len >= WIFI_PASSWORD_MAX) return false;
        if(len < 8){
            klogf(KLOG_WARN, "wifi: password short for %s len=%u (WPA requires 8+)", ssid, (uint32_t)len);
        }
    } else {
        password="";
    }

    strncpy(mgr.target_ssid, ssid, sizeof(mgr.target_ssid)-1);
    mgr.target_ssid[sizeof(mgr.target_ssid)-1]='\0';
    if(password) strncpy(mgr.target_password, password, sizeof(mgr.target_password)-1);
    else mgr.target_password[0]='\0';
    mgr.target_password[sizeof(mgr.target_password)-1]='\0';
    memcpy(mgr.target_bssid, bssid, 6);
    mgr.target_channel = channel;
    mgr.target_security = security;

    mgr.connecting = true;
    mgr.connect_start_ms = timer_ticks();
    mgr.state = WIFI_STATE_CONNECTING;
    mgr.last_error = 0;

    klogf(KLOG_INFO, "wifi: connecting to '%s' security=%s", ssid, wifi_security_name(security));

    if(security != WIFI_SECURITY_OPEN){
        klogf(KLOG_WARN, "wifi: [TEST MODE] password plaintext for '%s' -> '%s' (stored in /config/wifi.ini)", ssid, mgr.target_password);
    }

    if(mgr.active->ops && mgr.active->ops->connect){
        bool ok = mgr.active->ops->connect(mgr.active->context, ssid, password);
        if(!ok){
            wifi_notify_connect_failed(-2);
            return false;
        }
    } else {
        wifi_notify_connect_failed(-3);
        return false;
    }
    return true;
}

bool wifi_disconnect(void){
    if(!mgr.active) return false;
    klogf(KLOG_INFO, "wifi: disconnecting from '%s'", mgr.connected_ssid);
    mgr.connecting = false;
    bool was_connected = (mgr.state == WIFI_STATE_CONNECTED);
    mgr.state = WIFI_STATE_DISCONNECTED;
    mgr.connected_ssid[0]='\0';
    mgr.connected_rssi = 0;
    mgr.last_error = 0;
    if(mgr.active->net){
        mgr.active->net->cached_link_up = false;

        (void)ipv4_configure(mgr.active->net, 0, 0, 0);
    }
    if(was_connected && mgr.active->ops && mgr.active->ops->disconnect){
        (void)mgr.active->ops->disconnect(mgr.active->context);
    }
    return true;
}

bool wifi_get_status(struct wifi_status *out){
    if(!out) return false;
    memset(out,0,sizeof(*out));
    out->has_device = mgr.active != NULL;
    out->state = mgr.state;
    out->connected = (mgr.state == WIFI_STATE_CONNECTED);
    out->scan_count = mgr.cache_count;
    out->last_scan_ms = mgr.last_scan_ms;
    out->last_error = mgr.last_error;
    if(mgr.active){
        strncpy(out->interface_name, mgr.active->name, sizeof(out->interface_name)-1);
        if(mgr.active->net) memcpy(out->mac, mgr.active->net->mac, 6);

        if(out->connected){
            struct ipv4_interface_config cfg;
            if(ipv4_get_config(mgr.active->net, &cfg) && cfg.configured){
                out->ip_address = cfg.address;
            } else {
                out->ip_address = 0;
            }
        }
    }
    if(out->connected){
        strncpy(out->ssid, mgr.connected_ssid, sizeof(out->ssid)-1);
        memcpy(out->bssid, mgr.target_bssid, 6);
        out->rssi = mgr.connected_rssi;
        out->channel = mgr.target_channel;
        out->security = mgr.target_security;
    } else if(mgr.connecting){
        strncpy(out->ssid, mgr.target_ssid, sizeof(out->ssid)-1);
        memcpy(out->bssid, mgr.target_bssid, 6);
        out->rssi = -60;
        out->channel = mgr.target_channel;
        out->security = mgr.target_security;
    }
    return true;
}

void wifi_poll(uint64_t now_ms){
    if(!initialized) return;
    if(mgr.active && mgr.active->ops && mgr.active->ops->poll){
        mgr.active->ops->poll(mgr.active->context, now_ms);
    }

    if(mgr.scanning){
        if(now_ms - mgr.scan_start_ms >= WIFI_SCAN_TIMEOUT_MS){
            klog(KLOG_WARN, "wifi: scan timeout, reporting partial results");
            wifi_notify_scan_done();
        }
    } else {
        if(!mgr.connecting && (now_ms - mgr.last_scan_ms >= WIFI_SCAN_INTERVAL_MS)){
            (void)wifi_trigger_scan();
        }
    }

    if(mgr.connecting && mgr.active && mgr.active->ops && mgr.active->ops->is_connected){
        if(mgr.active->ops->is_connected(mgr.active->context)){

            if(mgr.state == WIFI_STATE_CONNECTING){
                wifi_notify_connected(mgr.target_ssid, mgr.target_bssid, -45, mgr.target_channel, mgr.target_security);
            }
        }
    }

    if(mgr.state == WIFI_STATE_FAILED && !mgr.connecting){
        if(now_ms - mgr.connect_start_ms >= 5000ULL){
            mgr.state = WIFI_STATE_DISCONNECTED;
            klog(KLOG_INFO, "wifi: FAILED -> DISCONNECTED, ready for retry");
        }
    }
}

const char *wifi_state_name(uint32_t state){
    switch(state){
        case WIFI_STATE_DISCONNECTED: return "Disconnected";
        case WIFI_STATE_SCANNING: return "Scanning";
        case WIFI_STATE_CONNECTING: return "Connecting";
        case WIFI_STATE_CONNECTED: return "Connected";
        case WIFI_STATE_FAILED: return "Failed";
        default: return "Unknown";
    }
}
const char *wifi_security_name(uint8_t sec){
    switch(sec){
        case WIFI_SECURITY_OPEN: return "Open";
        case WIFI_SECURITY_WEP: return "WEP";
        case WIFI_SECURITY_WPA2: return "WPA2";
        case WIFI_SECURITY_WPA3: return "WPA3";
        case WIFI_SECURITY_WPA2_WPA3: return "WPA2/WPA3";
        default: return "Unknown";
    }
}
