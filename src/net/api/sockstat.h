#pragma once

#include <stdint.h>

struct net_socket_entry;

int32_t net_socket_list(struct net_socket_entry *out, uint32_t capacity,
                        uint32_t offset);
