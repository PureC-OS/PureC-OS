/* Host test for src/net/transport/tcp.c: two endpoints share one stack over a
 * simulated lossy loopback with a simulated millisecond clock. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kernel/sync/mutex.h"
#include "net/transport/tcp.h"

#define ADDRESS 0x0A000001U
#define QUEUE_CAPACITY 256
#define TIME_WAIT_PAD_MS 130000U

static int failures;

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", __func__, __LINE__, #expr); \
        failures++; \
        return; \
    } \
} while (0)

/* ---- stubs for the kernel services tcp.c depends on ------------------- */

static uint64_t clock_ms = 1;
static struct net_device device = {.name = "lo", .mtu = 1500, .registered = true};
static ipv4_handler tcp_handler;

struct queued_packet {
    uint32_t source, destination;
    uint16_t length;
    uint8_t data[1500];
};
static struct queued_packet queue[QUEUE_CAPACITY];
static unsigned queue_head, queue_tail;

static unsigned loss_percent;
static unsigned reorder_percent;
static bool drop_next;
static bool corrupt_next;
static uint32_t rng_state = 12345;

static uint32_t rng(void) {
    rng_state = rng_state * 1664525U + 1013904223U;
    return rng_state >> 8;
}

uint64_t timer_ticks(void) { return clock_ms; }

int net_device_registered_dummy;
uint32_t net_device_count(void) { return 1; }
struct net_device *net_device_get(uint32_t index) { return index == 0 ? &device : 0; }

bool ipv4_get_config(struct net_device *dev, struct ipv4_interface_config *config) {
    if (dev != &device) return false;
    config->device = dev;
    config->address = ADDRESS;
    config->netmask = 0xFFFFFF00U;
    config->gateway = 0;
    config->configured = true;
    return true;
}

bool ipv4_register_handler(uint8_t protocol, ipv4_handler handler) {
    if (protocol == IPV4_PROTOCOL_TCP) tcp_handler = handler;
    return true;
}

enum ipv4_send_result ipv4_send_from(struct net_device *dev, uint32_t source,
                                     uint32_t destination, uint8_t protocol,
                                     const uint8_t *payload, uint16_t length) {
    (void)dev; (void)protocol;
    if (drop_next) { drop_next = false; return IPV4_SEND_OK; }
    if (loss_percent && rng() % 100 < loss_percent) return IPV4_SEND_OK;
    unsigned next = (queue_tail + 1) % QUEUE_CAPACITY;
    if (next == queue_head) return IPV4_SEND_OK;
    struct queued_packet *packet = &queue[queue_tail];
    packet->source = source;
    packet->destination = destination;
    packet->length = length;
    memcpy(packet->data, payload, length);
    if (corrupt_next) { corrupt_next = false; packet->data[length - 1] ^= 0xFF; }
    if (reorder_percent && queue_tail != queue_head && rng() % 100 < reorder_percent) {
        unsigned previous = (queue_tail + QUEUE_CAPACITY - 1) % QUEUE_CAPACITY;
        struct queued_packet swap = queue[previous];
        queue[previous] = queue[queue_tail];
        queue[queue_tail] = swap;
    }
    queue_tail = next;
    return IPV4_SEND_OK;
}

void mutex_lock(mutex_t *mutex) { (void)mutex; }
void mutex_unlock(mutex_t *mutex) { (void)mutex; }

static void pump_one_ms(void) {
    clock_ms++;
    unsigned count = (queue_tail + QUEUE_CAPACITY - queue_head) % QUEUE_CAPACITY;
    while (count--) {
        struct queued_packet packet = queue[queue_head];
        queue_head = (queue_head + 1) % QUEUE_CAPACITY;
        struct ipv4_packet ip = {
            .device = &device, .source = packet.source,
            .destination = packet.destination, .protocol = IPV4_PROTOCOL_TCP,
            .ttl = 64, .payload = packet.data, .payload_length = packet.length};
        tcp_handler(&ip);
    }
    tcp_poll(clock_ms);
}

void scheduler_sleep(uint32_t milliseconds) {
    while (milliseconds--) pump_one_ms();
}
void scheduler_yield(void) { pump_one_ms(); }

static void advance(uint32_t milliseconds) { scheduler_sleep(milliseconds); }

/* ---- helpers ---------------------------------------------------------- */

static void reset_stack(void) {
    queue_head = queue_tail = 0;
    loss_percent = reorder_percent = 0;
    drop_next = corrupt_next = false;
    CHECK(tcp_init());
    CHECK(tcp_handler != 0);
}

struct pair { int32_t listener, client, server; };

static bool open_pair(struct pair *pair, uint16_t port) {
    pair->listener = tcp_socket();
    if (pair->listener < 0 || tcp_listen(pair->listener, port, 2) != TCP_OK) return false;
    pair->client = tcp_socket();
    if (pair->client < 0) return false;
    if (tcp_connect(pair->client, ADDRESS, port, 5000) != TCP_OK) return false;
    pair->server = tcp_accept(pair->listener, 5000);
    return pair->server >= 0;
}

static bool wait_closed(int32_t handle, uint32_t limit_ms) {
    struct tcp_socket_info info;
    for (uint32_t waited = 0; waited < limit_ms; waited += 100) {
        if (!tcp_get_info(handle, &info)) return true;
        advance(100);
    }
    return !tcp_get_info(handle, &info);
}

/* Streams `total` bytes client->server (and optionally back) and verifies. */
static bool transfer(int32_t from, int32_t to, uint32_t total, uint32_t max_ms) {
    static uint8_t out[4096], in[4096];
    uint32_t sent = 0, received = 0;
    for (uint32_t elapsed = 0; elapsed < max_ms && received < total; elapsed++) {
        if (sent < total) {
            uint32_t chunk = total - sent > sizeof(out) ? sizeof(out) : total - sent;
            for (uint32_t i = 0; i < chunk; i++) out[i] = (uint8_t)((sent + i) * 31U + 7U);
            int32_t n = tcp_send(from, out, chunk, TCP_TIMEOUT_NONE);
            if (n > 0) sent += (uint32_t)n;
            else if (n != TCP_ERR_WOULD_BLOCK) return false;
        }
        int32_t n = tcp_receive(to, in, sizeof(in), TCP_TIMEOUT_NONE);
        if (n > 0) {
            for (int32_t i = 0; i < n; i++)
                if (in[i] != (uint8_t)((received + (uint32_t)i) * 31U + 7U)) {
                    fprintf(stderr, "data mismatch at offset %u\n", received + (uint32_t)i);
                    return false;
                }
            received += (uint32_t)n;
        } else if (n != TCP_ERR_WOULD_BLOCK) {
            fprintf(stderr, "receive returned %d after %u bytes\n", n, received);
            return false;
        }
        pump_one_ms();
    }
    if (received != total) fprintf(stderr, "only %u of %u bytes arrived\n", received, total);
    return received == total;
}

/* ---- tests ------------------------------------------------------------ */

static void test_handshake_and_echo(void) {
    reset_stack();
    struct pair p;
    CHECK(open_pair(&p, 8080));
    struct tcp_socket_info info;
    CHECK(tcp_get_info(p.client, &info) && info.state == TCP_STATE_ESTABLISHED);
    CHECK(tcp_get_info(p.server, &info) && info.state == TCP_STATE_ESTABLISHED);
    CHECK(info.remote_address == ADDRESS && info.local_port == 8080);

    CHECK(tcp_send(p.client, "hello", 5, 1000) == 5);
    char buffer[16] = {0};
    CHECK(tcp_receive(p.server, buffer, sizeof(buffer), 1000) == 5);
    CHECK(memcmp(buffer, "hello", 5) == 0);
    CHECK(tcp_send(p.server, "world!", 6, 1000) == 6);
    CHECK(tcp_receive(p.client, buffer, sizeof(buffer), 1000) == 6);
    CHECK(memcmp(buffer, "world!", 6) == 0);
    CHECK(tcp_receive(p.client, buffer, sizeof(buffer), TCP_TIMEOUT_NONE) == TCP_ERR_WOULD_BLOCK);
}

static void test_orderly_close(void) {
    reset_stack();
    struct pair p;
    CHECK(open_pair(&p, 9000));
    CHECK(tcp_send(p.client, "bye", 3, 1000) == 3);
    CHECK(tcp_close(p.client) == TCP_OK);
    char buffer[8];
    CHECK(tcp_receive(p.server, buffer, sizeof(buffer), 1000) == 3);
    CHECK(tcp_receive(p.server, buffer, sizeof(buffer), 1000) == 0);
    CHECK(tcp_close(p.server) == TCP_OK);
    CHECK(wait_closed(p.server, 2000));
    CHECK(wait_closed(p.client, TIME_WAIT_PAD_MS));
    CHECK(tcp_close(p.listener) == TCP_OK);
}

static void test_refused(void) {
    reset_stack();
    int32_t client = tcp_socket();
    CHECK(client >= 0);
    CHECK(tcp_connect(client, ADDRESS, 4242, 3000) == TCP_ERR_RESET);
    CHECK(tcp_close(client) == TCP_OK);
}

static void test_bulk_clean(void) {
    reset_stack();
    struct pair p;
    CHECK(open_pair(&p, 7000));
    CHECK(transfer(p.client, p.server, 1000000, 120000));
    struct tcp_stats stats;
    tcp_get_stats(&stats);
    CHECK(stats.retransmits == 0);
}

static void test_bulk_with_loss_and_reorder(void) {
    reset_stack();
    struct pair p;
    CHECK(open_pair(&p, 7001));
    loss_percent = 5;
    reorder_percent = 10;
    CHECK(transfer(p.client, p.server, 300000, 600000));
    loss_percent = reorder_percent = 0;
    struct tcp_stats stats;
    tcp_get_stats(&stats);
    CHECK(stats.retransmits + stats.fast_retransmits > 0);
    /* Connection must still be usable and close cleanly afterwards. */
    CHECK(tcp_send(p.server, "ok", 2, 5000) == 2);
    char buffer[4];
    CHECK(tcp_receive(p.client, buffer, sizeof(buffer), 5000) == 2);
    CHECK(tcp_close(p.client) == TCP_OK);
    CHECK(tcp_close(p.server) == TCP_OK);
    CHECK(wait_closed(p.client, TIME_WAIT_PAD_MS));
    CHECK(wait_closed(p.server, TIME_WAIT_PAD_MS));
}

static void test_bidirectional(void) {
    reset_stack();
    struct pair p;
    CHECK(open_pair(&p, 7002));
    loss_percent = 2;
    CHECK(transfer(p.client, p.server, 60000, 200000));
    CHECK(transfer(p.server, p.client, 60000, 200000));
}

static void test_lost_syn_is_retransmitted(void) {
    reset_stack();
    struct pair p;
    p.listener = tcp_socket();
    CHECK(tcp_listen(p.listener, 6000, 1) == TCP_OK);
    p.client = tcp_socket();
    drop_next = true; /* first SYN vanishes */
    CHECK(tcp_connect(p.client, ADDRESS, 6000, 10000) == TCP_OK);
    CHECK(tcp_accept(p.listener, 1000) >= 0);
}

static void test_zero_window_recovers(void) {
    reset_stack();
    struct pair p;
    CHECK(open_pair(&p, 6100));
    static uint8_t block[TCP_BUFFER_SIZE];
    memset(block, 0xAB, sizeof(block));
    uint32_t sent = 0;
    /* Fill the receiver's window and our own send buffer without reading. */
    for (int round = 0; round < 3000 && sent < 3 * TCP_BUFFER_SIZE; round++) {
        int32_t n = tcp_send(p.client, block, 1024, TCP_TIMEOUT_NONE);
        if (n > 0) sent += (uint32_t)n;
        pump_one_ms();
    }
    struct tcp_socket_info info;
    CHECK(tcp_get_info(p.server, &info) && info.bytes_unread == TCP_BUFFER_SIZE);
    CHECK(tcp_get_info(p.client, &info) && info.bytes_unsent > 0);
    uint32_t drained = 0;
    static uint8_t sink[2048];
    for (int round = 0; round < 60000 && drained < sent; round++) {
        int32_t n = tcp_receive(p.server, sink, sizeof(sink), TCP_TIMEOUT_NONE);
        if (n > 0) drained += (uint32_t)n;
        pump_one_ms();
    }
    CHECK(drained == sent);
}

static void test_corrupt_segment_dropped(void) {
    reset_stack();
    struct pair p;
    CHECK(open_pair(&p, 6200));
    corrupt_next = true;
    CHECK(tcp_send(p.client, "data", 4, 1000) == 4);
    char buffer[8];
    /* Corrupted copy is discarded, the retransmission delivers it. */
    CHECK(tcp_receive(p.server, buffer, sizeof(buffer), 5000) == 4);
    struct tcp_stats stats;
    tcp_get_stats(&stats);
    CHECK(stats.bad_checksum == 1);
}

static void test_abort_resets_peer(void) {
    reset_stack();
    struct pair p;
    CHECK(open_pair(&p, 6300));
    CHECK(tcp_abort(p.client) == TCP_OK);
    char buffer[4];
    CHECK(tcp_receive(p.server, buffer, sizeof(buffer), 1000) == TCP_ERR_RESET);
}

static void test_peer_vanishes_times_out(void) {
    reset_stack();
    struct pair p;
    CHECK(open_pair(&p, 6400));
    loss_percent = 100; /* network goes dark */
    CHECK(tcp_send(p.client, "x", 1, 1000) == 1);
    int32_t result = TCP_OK;
    char buffer[4];
    for (int i = 0; i < 400 && result == TCP_OK; i++) {
        advance(1000);
        struct tcp_socket_info info;
        if (!tcp_get_info(p.client, &info) || info.state == TCP_STATE_CLOSED) {
            result = tcp_receive(p.client, buffer, sizeof(buffer), TCP_TIMEOUT_NONE);
            break;
        }
    }
    CHECK(result == TCP_ERR_TIMEOUT);
}

static void test_backlog_and_port_reuse_checks(void) {
    reset_stack();
    int32_t a = tcp_socket(), b = tcp_socket();
    CHECK(tcp_listen(a, 5000, 1) == TCP_OK);
    CHECK(tcp_listen(b, 5000, 1) == TCP_ERR_IN_USE);
    CHECK(tcp_accept(a, TCP_TIMEOUT_NONE) == TCP_ERR_WOULD_BLOCK);
    CHECK(tcp_connect(b, 0, 80, 100) == TCP_ERR_INVALID);
    for (int i = 0; i < TCP_SOCKET_CAPACITY; i++) (void)tcp_socket();
    CHECK(tcp_socket() == TCP_ERR_NO_SOCKET);
}

static void test_owner_cleanup(void) {
    reset_stack();
    int32_t listener = tcp_socket_owned(7);
    CHECK(listener >= 0 && tcp_is_owner(listener, 7) && !tcp_is_owner(listener, 8));
    CHECK(tcp_listen(listener, 5500, 2) == TCP_OK);
    int32_t client = tcp_socket_owned(9);
    CHECK(tcp_connect(client, ADDRESS, 5500, 3000) == TCP_OK);
    int32_t server = tcp_accept(listener, 1000);
    CHECK(server >= 0 && tcp_is_owner(server, 7)); /* accepted child inherits */
    /* Owner 7 "exits": its sockets vanish and the peer is reset. */
    tcp_release_owner(7);
    struct tcp_socket_info info;
    CHECK(!tcp_get_info(listener, &info) && !tcp_get_info(server, &info));
    char buffer[4];
    CHECK(tcp_receive(client, buffer, sizeof(buffer), 1000) == TCP_ERR_RESET);
}

int main(void) {
    test_handshake_and_echo();
    test_orderly_close();
    test_refused();
    test_bulk_clean();
    test_bulk_with_loss_and_reorder();
    test_bidirectional();
    test_lost_syn_is_retransmitted();
    test_zero_window_recovers();
    test_corrupt_segment_dropped();
    test_abort_resets_peer();
    test_peer_vanishes_times_out();
    test_backlog_and_port_reuse_checks();
    test_owner_cleanup();
    if (failures) {
        fprintf(stderr, "%d tcp test failure(s)\n", failures);
        return 1;
    }
    puts("tcp tests passed");
    return 0;
}
