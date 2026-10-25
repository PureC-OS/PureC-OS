#pragma once

#include "../core/net_device.h"
#include <stdbool.h>
#include <stdint.h>

bool dhcp_init(void);
bool dhcp_start(struct net_device *device);
void dhcp_poll(uint64_t now_ms);
bool dhcp_is_bound(struct net_device *device);
