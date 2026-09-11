#pragma once

#include "../core/net_device.h"
#include <stdbool.h>
#include <stdint.h>

#define ETHERNET_ADDRESS_LENGTH 6
#define ETHERNET_HEADER_LENGTH 14
#define ETHERNET_TYPE_IPV4 0x0800
#define ETHERNET_TYPE_ARP 0x0806

struct ethernet_packet {
    struct net_device *device;
    uint8_t destination[ETHERNET_ADDRESS_LENGTH];
    uint8_t source[ETHERNET_ADDRESS_LENGTH];
    uint16_t ethertype;
    const uint8_t *payload;
    uint16_t payload_length;
};

typedef void (*ethernet_handler)(const struct ethernet_packet *packet);

struct ethernet_stats {
    uint64_t received;
    uint64_t transmitted;
    uint64_t malformed;
    uint64_t unsupported;
    uint64_t transmit_errors;
};

void ethernet_init(void);
bool ethernet_register_handler(uint16_t ethertype, ethernet_handler handler);
bool ethernet_receive(struct net_device *device,
                      const uint8_t *frame, uint16_t length);
bool ethernet_send(struct net_device *device, const uint8_t destination[6],
                   uint16_t ethertype, const uint8_t *payload,
                   uint16_t payload_length);
void ethernet_get_stats(struct ethernet_stats *stats);
