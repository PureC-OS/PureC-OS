#pragma once

#include "../network/ipv4.h"
#include <stdbool.h>
#include <stdint.h>

#define TCP_HEADER_LENGTH 20
#define TCP_SOCKET_CAPACITY 16
#define TCP_BUFFER_SIZE 16384
#define TCP_MAX_MSS (IPV4_PAYLOAD_MAX_LENGTH-TCP_HEADER_LENGTH)
#define TCP_ACCEPT_BACKLOG_MAX 4
#define TCP_TIMEOUT_NONE 0U
#define TCP_TIMEOUT_FOREVER 0xFFFFFFFFU

#define TCP_OK 0
#define TCP_ERR_INVALID (-1)
#define TCP_ERR_NO_SOCKET (-2)
#define TCP_ERR_NOT_CONNECTED (-3)
#define TCP_ERR_RESET (-4)
#define TCP_ERR_TIMEOUT (-5)
#define TCP_ERR_NO_ROUTE (-6)
#define TCP_ERR_IN_USE (-7)
#define TCP_ERR_WOULD_BLOCK (-8)

enum tcp_state {
    TCP_STATE_CLOSED,
    TCP_STATE_LISTEN,
    TCP_STATE_SYN_SENT,
    TCP_STATE_SYN_RECEIVED,
    TCP_STATE_ESTABLISHED,
    TCP_STATE_FIN_WAIT_1,
    TCP_STATE_FIN_WAIT_2,
    TCP_STATE_CLOSE_WAIT,
    TCP_STATE_CLOSING,
    TCP_STATE_LAST_ACK,
    TCP_STATE_TIME_WAIT
};

struct tcp_stats {
    uint64_t segments_received;
    uint64_t segments_sent;
    uint64_t bad_checksum;
    uint64_t retransmits;
    uint64_t fast_retransmits;
    uint64_t resets_sent;
    uint64_t connections_established;
    uint64_t connections_timed_out;
};

struct tcp_socket_info {
    enum tcp_state state;
    uint32_t local_address;
    uint32_t remote_address;
    uint16_t local_port;
    uint16_t remote_port;
    uint32_t bytes_unread;
    uint32_t bytes_unsent;
    uint32_t cwnd;
    uint32_t rto_ms;
};

bool tcp_init(void);
void tcp_poll(uint64_t now_ms);
int32_t tcp_socket(void);
int32_t tcp_socket_owned(uint32_t owner);
bool tcp_is_owner(int32_t handle, uint32_t owner);
void tcp_release_owner(uint32_t owner);
int32_t tcp_connect(int32_t handle, uint32_t address, uint16_t port,
                    uint32_t timeout_ms);
int32_t tcp_listen(int32_t handle, uint16_t port, uint8_t backlog);
int32_t tcp_accept(int32_t handle, uint32_t timeout_ms);
int32_t tcp_send(int32_t handle, const void *data, uint32_t length,
                 uint32_t timeout_ms);
int32_t tcp_receive(int32_t handle, void *data, uint32_t length,
                    uint32_t timeout_ms);
int32_t tcp_close(int32_t handle);
int32_t tcp_abort(int32_t handle);
bool tcp_get_info(int32_t handle, struct tcp_socket_info *info);
void tcp_get_stats(struct tcp_stats *stats);
