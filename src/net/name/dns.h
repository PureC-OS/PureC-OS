#pragma once

#include "../core/net_device.h"
#include <stdbool.h>
#include <stdint.h>

#define DNS_HOSTNAME_CAPACITY 128

bool dns_init(void);
bool dns_set_server(struct net_device *device, uint32_t server);
uint32_t dns_get_server(struct net_device *device);
