#pragma once

#include "udp.h"
#include <stdbool.h>
#include <stdint.h>

#define UDPE_LOOPBACK_ADDRESS 0x7F000001U

int32_t udpe_create(void);
void udpe_release(int32_t id);
int32_t udpe_bind(int32_t id, uint32_t address, uint16_t port, bool reuse);
int32_t udpe_connect(int32_t id, uint32_t address, uint16_t port);
int32_t udpe_sendto(int32_t id, const void *buffer, uint32_t length,
                    bool has_target, uint32_t address, uint16_t port);
int32_t udpe_recvfrom(int32_t id, void *buffer, uint32_t length, bool nonblock,
                      uint32_t timeout_ms, bool peek, uint32_t *address,
                      uint16_t *port);
int32_t udpe_poll(int32_t id);
int32_t udpe_set_broadcast(int32_t id, bool enabled);
int32_t udpe_bind_device(int32_t id, const char *name);
int32_t udpe_name(int32_t id, bool peer, uint32_t *address, uint16_t *port);
bool udpe_port_in_use(uint16_t port);

#define UDPE_SLOT_COUNT 128U

struct udpe_info {
  bool bound;
  bool connected;
  bool broadcast;
  bool reuse;
  uint32_t local_address;
  uint16_t local_port;
  uint32_t peer_address;
  uint16_t peer_port;
  uint32_t queued_bytes;
  uint32_t dropped;
  char device[16];
};

bool udpe_info(int32_t id, struct udpe_info *out);
bool udpe_deliver(const struct udp_datagram *datagram);
