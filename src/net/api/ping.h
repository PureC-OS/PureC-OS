#pragma once

#include <stdint.h>

enum net_ping_status {
    NET_PING_OK=0,
    NET_PING_INVALID=-1,
    NET_PING_NO_INTERFACE=-2,
    NET_PING_NOT_CONFIGURED=-3,
    NET_PING_RESOLVE_FAILED=-4,
    NET_PING_TIMEOUT=-5,
    NET_PING_BUSY=-6,
    NET_PING_NETWORK_ERROR=-7
};

struct net_ping_reply {
    uint32_t address;
    uint32_t round_trip_ms;
    uint16_t sequence;
    uint8_t ttl;
};

enum net_ping_status net_ping_target(const char *target, uint16_t sequence,
                                     uint32_t timeout_ms,
                                     struct net_ping_reply *reply);
