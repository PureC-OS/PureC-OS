#pragma once

#include "../core/net_device.h"
#include <stdbool.h>
#include <stdint.h>

enum icmp_ping_status {
    ICMP_PING_OK=0,
    ICMP_PING_TIMEOUT=-1,
    ICMP_PING_BUSY=-2,
    ICMP_PING_NETWORK=-3
};

struct icmp_ping_reply {
    uint32_t address;
    uint32_t round_trip_ms;
    uint16_t sequence;
    uint8_t ttl;
};

bool icmp_init(void);
enum icmp_ping_status icmp_ping(struct net_device *device, uint32_t address,
                                uint16_t sequence, uint32_t timeout_ms,
                                struct icmp_ping_reply *reply);
