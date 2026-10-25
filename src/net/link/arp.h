#pragma once

#include "../core/net_device.h"
#include <stdbool.h>
#include <stdint.h>

#define ARP_CACHE_CAPACITY 16
#define NET_IPV4_ADDRESS(a,b,c,d) \
    (((uint32_t)(a)<<24)|((uint32_t)(b)<<16)|((uint32_t)(c)<<8)|(uint32_t)(d))

struct arp_cache_record {
    struct net_device *device;
    uint32_t ipv4;
    uint8_t mac[6];
    uint64_t age_ms;
    bool valid;
    bool pending;
};

struct arp_stats {
    uint64_t requests_received;
    uint64_t replies_received;
    uint64_t requests_sent;
    uint64_t replies_sent;
    uint64_t malformed;
    uint64_t cache_updates;
    uint64_t cache_expirations;
    uint64_t address_conflicts;
};

bool arp_init(void);
bool arp_set_local_ipv4(struct net_device *device, uint32_t address);
uint32_t arp_get_local_ipv4(struct net_device *device);
bool arp_lookup(struct net_device *device, uint32_t ipv4, uint8_t mac[6]);
bool arp_resolve(struct net_device *device, uint32_t ipv4, uint8_t mac[6]);
void arp_poll(uint64_t now_ms);
uint32_t arp_cache_snapshot(struct arp_cache_record *records,
                            uint32_t capacity);
void arp_get_stats(struct arp_stats *stats);
