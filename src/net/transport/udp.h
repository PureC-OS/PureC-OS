#pragma once

#include "../network/ipv4.h"
#include <stdbool.h>
#include <stdint.h>

#define UDP_HEADER_LENGTH 8
#define UDP_PAYLOAD_MAX_LENGTH (IPV4_PAYLOAD_MAX_LENGTH-UDP_HEADER_LENGTH)

struct udp_datagram {
    struct net_device *device;
    uint32_t source_address;
    uint32_t destination_address;
    uint16_t source_port;
    uint16_t destination_port;
    const uint8_t *payload;
    uint16_t payload_length;
};

typedef void (*udp_handler)(const struct udp_datagram *datagram);

bool udp_init(void);
bool udp_bind(uint16_t port, udp_handler handler);
enum ipv4_send_result udp_send(struct net_device *device,
                               uint32_t destination_address,
                               uint16_t source_port, uint16_t destination_port,
                               const uint8_t *payload, uint16_t length);
enum ipv4_send_result udp_send_from(struct net_device *device,
                                    uint32_t source_address,
                                    uint32_t destination_address,
                                    uint16_t source_port,
                                    uint16_t destination_port,
                                    const uint8_t *payload, uint16_t length);
