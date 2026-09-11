#pragma once

#include "../core/net_device.h"
#include <stdbool.h>
#include <stdint.h>

#define IPV4_HEADER_MIN_LENGTH 20
#define IPV4_PAYLOAD_MAX_LENGTH (NET_ETHERNET_MTU-IPV4_HEADER_MIN_LENGTH)
#define IPV4_PROTOCOL_ICMP 1
#define IPV4_PROTOCOL_UDP 17
#define IPV4_BROADCAST 0xFFFFFFFFU

enum ipv4_send_result {
    IPV4_SEND_ERROR=-1,
    IPV4_SEND_WAITING_ARP=0,
    IPV4_SEND_OK=1
};

struct ipv4_packet {
    struct net_device *device;
    uint32_t source;
    uint32_t destination;
    uint8_t protocol;
    uint8_t ttl;
    const uint8_t *payload;
    uint16_t payload_length;
};

struct ipv4_interface_config {
    struct net_device *device;
    uint32_t address;
    uint32_t netmask;
    uint32_t gateway;
    bool configured;
};

typedef void (*ipv4_handler)(const struct ipv4_packet *packet);

bool ipv4_init(void);
bool ipv4_register_handler(uint8_t protocol, ipv4_handler handler);
bool ipv4_configure(struct net_device *device, uint32_t address,
                    uint32_t netmask, uint32_t gateway);
bool ipv4_get_config(struct net_device *device,
                     struct ipv4_interface_config *config);
enum ipv4_send_result ipv4_send(struct net_device *device,
                                uint32_t destination, uint8_t protocol,
                                const uint8_t *payload, uint16_t length);
enum ipv4_send_result ipv4_send_from(struct net_device *device,
                                     uint32_t source, uint32_t destination,
                                     uint8_t protocol, const uint8_t *payload,
                                     uint16_t length);
uint16_t ipv4_checksum(const uint8_t *data, uint16_t length);
bool ipv4_parse_address(const char *text, uint32_t *address);
