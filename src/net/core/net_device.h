#pragma once

#include <stdbool.h>
#include <stdint.h>

#define NET_DEVICE_NAME_CAPACITY 16
#define NET_DEVICE_MAX_COUNT 4
#define NET_DEVICE_RX_QUEUE_LENGTH 16
#define NET_ETHERNET_MTU 1500
#define NET_ETHERNET_MAX_FRAME_SIZE 1522

struct net_device;

struct net_device_ops {
    bool (*transmit)(void *context, const uint8_t *frame, uint16_t length);
    void (*poll)(void *context, uint32_t budget);
    bool (*link_up)(void *context);
};

struct net_device_stats {
    uint64_t rx_packets;
    uint64_t rx_bytes;
    uint64_t rx_dropped;
    uint64_t rx_errors;
    uint64_t tx_packets;
    uint64_t tx_bytes;
    uint64_t tx_dropped;
    uint64_t tx_errors;
};

struct net_frame {
    uint16_t length;
    uint8_t data[NET_ETHERNET_MAX_FRAME_SIZE];
};

struct net_device {
    char name[NET_DEVICE_NAME_CAPACITY];
    uint8_t mac[6];
    uint16_t mtu;
    const struct net_device_ops *ops;
    void *driver_context;
    struct net_device_stats stats;
    bool registered;
    bool cached_link_up;
};

void net_device_registry_init(void);
bool net_device_register(struct net_device *device);
uint32_t net_device_count(void);
struct net_device *net_device_get(uint32_t index);
bool net_device_transmit(struct net_device *device,
                         const uint8_t *frame, uint16_t length);
void net_device_poll_all(uint32_t budget_per_device);
bool net_device_receive(struct net_device *device,
                        const uint8_t *frame, uint16_t length);
bool net_device_dequeue(struct net_device *device, struct net_frame *frame);
