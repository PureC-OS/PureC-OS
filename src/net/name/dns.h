#pragma once

#include "../core/net_device.h"
#include <stdbool.h>
#include <stdint.h>

#define DNS_HOSTNAME_CAPACITY 128

enum dns_result {
    DNS_RESULT_OK=0,
    DNS_RESULT_INVALID=-1,
    DNS_RESULT_NO_SERVER=-2,
    DNS_RESULT_BUSY=-3,
    DNS_RESULT_TIMEOUT=-4,
    DNS_RESULT_NOT_FOUND=-5,
    DNS_RESULT_NETWORK=-6
};

bool dns_init(void);
bool dns_set_server(struct net_device *device, uint32_t server);
enum dns_result dns_resolve_ipv4(struct net_device *device,
                                 const char *hostname, uint32_t timeout_ms,
                                 uint32_t *address);
