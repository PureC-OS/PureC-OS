#pragma once

#include <stdbool.h>
#include <stdint.h>

#define SOCK_AF_INET 2
#define SOCK_TYPE_STREAM 1
#define SOCK_TYPE_DGRAM 2
#define SOCK_IPPROTO_UDP 17
#define SOCK_SO_BROADCAST 6
#define SOCK_SO_BINDTODEVICE 25
#define SOCK_TYPE_MASK 0xFU
#define SOCK_FLAG_NONBLOCK 04000U
#define SOCK_FLAG_CLOEXEC 02000000U

#define SOCK_MSG_PEEK 0x2U
#define SOCK_MSG_DONTWAIT 0x40U
#define SOCK_MSG_NOSIGNAL 0x4000U

#define SOCK_SOL_SOCKET 1
#define SOCK_IPPROTO_TCP 6
#define SOCK_SO_REUSEADDR 2
#define SOCK_SO_TYPE 3
#define SOCK_SO_ERROR 4
#define SOCK_SO_SNDBUF 7
#define SOCK_SO_RCVBUF 8
#define SOCK_SO_KEEPALIVE 9
#define SOCK_SO_LINGER 13
#define SOCK_SO_RCVTIMEO 20
#define SOCK_SO_SNDTIMEO 21
#define SOCK_TCP_NODELAY 1

struct sock_addr_in {
    uint16_t family;
    uint16_t port_be;
    uint32_t address_be;
    uint8_t zero[8];
};

struct sock_timeval {
    int64_t seconds;
    int64_t microseconds;
};

int32_t sock_create(int32_t domain, int32_t type, int32_t protocol);
void sock_release(int32_t id);
int32_t sock_connect(int32_t id, const struct sock_addr_in *address, bool nonblock);
int32_t sock_bind(int32_t id, const struct sock_addr_in *address);
int32_t sock_listen(int32_t id, int32_t backlog);
int32_t sock_accept(int32_t id, bool nonblock, int32_t *new_id,
                    struct sock_addr_in *peer);
int32_t sock_read(int32_t id, void *buffer, uint32_t count, bool nonblock,
                  uint32_t flags);
int32_t sock_write(int32_t id, const void *buffer, uint32_t count,
                   bool nonblock, uint32_t flags);
int32_t sock_shutdown(int32_t id, int32_t how);
int32_t sock_sendto(int32_t id, const void *buffer, uint32_t count,
                    bool nonblock, uint32_t flags,
                    const struct sock_addr_in *target);
int32_t sock_recvfrom(int32_t id, void *buffer, uint32_t count, bool nonblock,
                      uint32_t flags, struct sock_addr_in *source);
int32_t sock_poll(int32_t id);
int32_t sock_name(int32_t id, bool peer, struct sock_addr_in *out);
int32_t sock_setopt(int32_t id, int32_t level, int32_t name,
                    const void *value, uint32_t length);
int32_t sock_getopt(int32_t id, int32_t level, int32_t name, void *value,
                    uint32_t *length);
int32_t sock_resolve(const char *name, uint32_t timeout_ms, uint32_t *address_be);
