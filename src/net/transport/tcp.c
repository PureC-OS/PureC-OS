#include "tcp.h"
#include "../util/byteorder.h"
#include "../../drivers/interrupts/timer.h"
#include "../../kernel/process/scheduler.h"
#include "../../kernel/sync/mutex.h"
#include "../../lib/string.h"

#define TCP_FLAG_FIN 0x01
#define TCP_FLAG_SYN 0x02
#define TCP_FLAG_RST 0x04
#define TCP_FLAG_PSH 0x08
#define TCP_FLAG_ACK 0x10

#define TCP_OPTION_END 0
#define TCP_OPTION_NOP 1
#define TCP_OPTION_MSS 2

#define TCP_DEFAULT_MSS 536U
#define TCP_INITIAL_RTO_MS 1000U
#define TCP_MIN_RTO_MS 200U
#define TCP_MAX_RTO_MS 60000U
#define TCP_MAX_RETRIES 8U
#define TCP_MAX_SYN_RETRIES 5U
#define TCP_MAX_ARP_RETRIES 40U
#define TCP_ARP_RETRY_MS 50U
#define TCP_TIME_WAIT_MS 30000U
#define TCP_EPHEMERAL_FIRST 49152U
#define TCP_EPHEMERAL_LAST 65535U
#define TCP_INITIAL_CWND_SEGMENTS 3U

#define SEQ_LT(a, b) ((int32_t)((a) - (b)) < 0)
#define SEQ_LEQ(a, b) ((int32_t)((a) - (b)) <= 0)
#define SEQ_GT(a, b) ((int32_t)((a) - (b)) > 0)

struct tcp_socket
{
    bool used;
    bool app_closed;
    enum tcp_state state;
    int error;
    struct net_device *device;
    uint32_t local_address, remote_address;
    uint16_t local_port, remote_port;
    int8_t parent;
    uint8_t backlog;
    int8_t accept_queue[TCP_ACCEPT_BACKLOG_MAX];
    uint8_t accept_count;

    uint32_t iss, snd_una, snd_nxt, snd_max, snd_wnd, snd_wl1, snd_wl2;
    uint32_t irs, rcv_nxt;
    uint32_t mss, cwnd, ssthresh;
    uint8_t dup_acks;
    bool in_recovery;

    bool fin_queued, fin_seq_set, fin_received;
    uint32_t fin_seq;

    bool timer_armed, arp_pending;
    uint64_t timer_deadline;
    uint32_t rto_ms, srtt_ms, rttvar_ms;
    uint8_t retries, arp_tries;
    bool rtt_active, rtt_valid;
    uint32_t rtt_seq;
    uint64_t rtt_start;
    uint64_t time_wait_deadline;

    uint32_t tx_start, tx_length;
    uint32_t rx_start, rx_length;
    uint8_t tx_buffer[TCP_BUFFER_SIZE];
    uint8_t rx_buffer[TCP_BUFFER_SIZE];
};

static struct tcp_socket sockets[TCP_SOCKET_CAPACITY];
static mutex_t tcp_lock;
static struct tcp_stats stats;
static uint16_t next_ephemeral = TCP_EPHEMERAL_FIRST;
static uint32_t isn_counter;
static uint8_t segment_buffer[NET_ETHERNET_MTU];

static uint64_t now_ms(void) { return timer_ticks(); }

static uint32_t checksum_add(uint32_t sum, const uint8_t *data, uint32_t length)
{
    while (length >= 2)
    {
        sum += net_read_be16(data);
        data += 2;
        length -= 2;
    }
    if (length)
        sum += (uint32_t)data[0] << 8;
    return sum;
}

static uint16_t tcp_checksum(uint32_t source, uint32_t destination,
                             const uint8_t *segment, uint16_t length)
{
    uint8_t pseudo[12];
    net_write_be32(pseudo, source);
    net_write_be32(pseudo + 4, destination);
    pseudo[8] = 0;
    pseudo[9] = IPV4_PROTOCOL_TCP;
    net_write_be16(pseudo + 10, length);
    uint32_t sum = checksum_add(0, pseudo, sizeof(pseudo));
    sum = checksum_add(sum, segment, length);
    while (sum >> 16)
        sum = (sum & 0xFFFFU) + (sum >> 16);
    return (uint16_t)~sum;
}

static uint32_t generate_isn(void)
{
    isn_counter += 64000U;
    return (uint32_t)(now_ms() * 250U) + isn_counter;
}

static uint32_t receive_window(const struct tcp_socket *s)
{
    uint32_t free_space = TCP_BUFFER_SIZE - s->rx_length;
    return free_space > 0xFFFFU ? 0xFFFFU : free_space;
}

static uint32_t min_u32(uint32_t a, uint32_t b) { return a < b ? a : b; }
static uint32_t max_u32(uint32_t a, uint32_t b) { return a > b ? a : b; }

static void ring_write(uint8_t *ring, uint32_t start, uint32_t offset,
                       const uint8_t *data, uint32_t length)
{
    uint32_t position = (start + offset) % TCP_BUFFER_SIZE;
    uint32_t first = min_u32(length, TCP_BUFFER_SIZE - position);
    memcpy(ring + position, data, first);
    if (length > first)
        memcpy(ring, data + first, length - first);
}

static void ring_read(const uint8_t *ring, uint32_t start, uint32_t offset,
                      uint8_t *out, uint32_t length)
{
    uint32_t position = (start + offset) % TCP_BUFFER_SIZE;
    uint32_t first = min_u32(length, TCP_BUFFER_SIZE - position);
    memcpy(out, ring + position, first);
    if (length > first)
        memcpy(out + first, ring, length - first);
}

static void socket_reset(struct tcp_socket *s)
{
    memset(s, 0, sizeof(*s));
    s->parent = -1;
}

static int32_t socket_index(const struct tcp_socket *s) { return (int32_t)(s - sockets); }

static struct tcp_socket *socket_from_handle(int32_t handle)
{
    if (handle < 0 || handle >= TCP_SOCKET_CAPACITY || !sockets[handle].used)
        return 0;
    return &sockets[handle];
}

static struct tcp_socket *socket_allocate(void)
{
    for (uint32_t index = 0; index < TCP_SOCKET_CAPACITY; index++)
    {
        if (sockets[index].used)
            continue;
        socket_reset(&sockets[index]);
        sockets[index].used = true;
        sockets[index].state = TCP_STATE_CLOSED;
        sockets[index].rto_ms = TCP_INITIAL_RTO_MS;
        return &sockets[index];
    }
    return 0;
}

static void socket_release(struct tcp_socket *s)
{
    socket_reset(s);
}

static void enter_closed(struct tcp_socket *s, int error)
{
    if (s->error == 0)
        s->error = error;
    s->state = TCP_STATE_CLOSED;
    s->timer_armed = false;
    s->fin_received = true;
    if (s->app_closed)
        socket_release(s);
}

static void arm_timer(struct tcp_socket *s)
{
    s->timer_armed = true;
    s->timer_deadline = now_ms() + (s->arp_pending ? TCP_ARP_RETRY_MS : s->rto_ms);
}

/* Emits one segment for socket `s`; sequence/ack come from the caller. */
static void emit_segment(struct tcp_socket *s, uint32_t sequence,
                         uint8_t flags, const uint8_t *data, uint16_t length,
                         bool with_mss)
{
    uint16_t header_length = with_mss ? TCP_HEADER_LENGTH + 4 : TCP_HEADER_LENGTH;
    if (header_length + length > sizeof(segment_buffer))
        return;
    uint8_t *segment = segment_buffer;
    net_write_be16(segment, s->local_port);
    net_write_be16(segment + 2, s->remote_port);
    net_write_be32(segment + 4, sequence);
    net_write_be32(segment + 8, (flags & TCP_FLAG_ACK) ? s->rcv_nxt : 0);
    segment[12] = (uint8_t)((header_length / 4U) << 4);
    segment[13] = flags;
    net_write_be16(segment + 14, (uint16_t)receive_window(s));
    net_write_be16(segment + 16, 0);
    net_write_be16(segment + 18, 0);
    if (with_mss)
    {
        segment[20] = TCP_OPTION_MSS;
        segment[21] = 4;
        net_write_be16(segment + 22, (uint16_t)TCP_MAX_MSS);
    }
    if (length)
        memcpy(segment + header_length, data, length);
    uint16_t total = (uint16_t)(header_length + length);
    net_write_be16(segment + 16, tcp_checksum(s->local_address, s->remote_address,
                                              segment, total));
    stats.segments_sent++;
    enum ipv4_send_result result = ipv4_send_from(
        s->device, s->local_address, s->remote_address, IPV4_PROTOCOL_TCP,
        segment, total);
    s->arp_pending = (result == IPV4_SEND_WAITING_ARP);
}

static void send_reset(struct net_device *device, uint32_t local_address,
                       uint32_t remote_address, uint16_t local_port,
                       uint16_t remote_port, uint32_t sequence, uint32_t ack,
                       uint8_t flags)
{
    uint8_t segment[TCP_HEADER_LENGTH];
    net_write_be16(segment, local_port);
    net_write_be16(segment + 2, remote_port);
    net_write_be32(segment + 4, sequence);
    net_write_be32(segment + 8, ack);
    segment[12] = (TCP_HEADER_LENGTH / 4U) << 4;
    segment[13] = flags;
    net_write_be16(segment + 14, 0);
    net_write_be16(segment + 16, 0);
    net_write_be16(segment + 18, 0);
    net_write_be16(segment + 16, tcp_checksum(local_address, remote_address,
                                              segment, sizeof(segment)));
    stats.resets_sent++;
    stats.segments_sent++;
    (void)ipv4_send_from(device, local_address, remote_address, IPV4_PROTOCOL_TCP,
                         segment, sizeof(segment));
}

static void send_ack(struct tcp_socket *s)
{
    emit_segment(s, s->snd_nxt, TCP_FLAG_ACK, 0, 0, false);
}

static void send_syn(struct tcp_socket *s)
{
    uint8_t flags = TCP_FLAG_SYN;
    if (s->state == TCP_STATE_SYN_RECEIVED)
        flags |= TCP_FLAG_ACK;
    emit_segment(s, s->iss, flags, 0, 0, true);
}

static void note_send_for_rtt(struct tcp_socket *s, uint32_t end_sequence)
{
    if (s->rtt_active)
        return;
    s->rtt_active = true;
    s->rtt_seq = end_sequence;
    s->rtt_start = now_ms();
}

static void rtt_sample(struct tcp_socket *s, uint32_t sample)
{
    if (!s->rtt_valid)
    {
        s->srtt_ms = sample;
        s->rttvar_ms = sample / 2;
        s->rtt_valid = true;
    }
    else
    {
        uint32_t delta = sample > s->srtt_ms ? sample - s->srtt_ms : s->srtt_ms - sample;
        s->rttvar_ms = (3U * s->rttvar_ms + delta) / 4U;
        s->srtt_ms = (7U * s->srtt_ms + sample) / 8U;
    }
    uint32_t rto = s->srtt_ms + max_u32(4U * s->rttvar_ms, 10U);
    if (rto < TCP_MIN_RTO_MS)
        rto = TCP_MIN_RTO_MS;
    if (rto > TCP_MAX_RTO_MS)
        rto = TCP_MAX_RTO_MS;
    s->rto_ms = rto;
}

static uint32_t unsent_bytes(const struct tcp_socket *s)
{
    uint32_t offset = s->snd_nxt - s->snd_una;
    return offset >= s->tx_length ? 0 : s->tx_length - offset;
}

/* Sends as much queued data (and finally FIN) as the windows allow. */
static void tcp_output(struct tcp_socket *s)
{
    if (s->state != TCP_STATE_ESTABLISHED && s->state != TCP_STATE_CLOSE_WAIT && s->state != TCP_STATE_FIN_WAIT_1 && s->state != TCP_STATE_CLOSING && s->state != TCP_STATE_LAST_ACK)
        return;
    uint32_t window = min_u32(s->snd_wnd, s->cwnd);
    for (;;)
    {
        uint32_t in_flight = s->snd_nxt - s->snd_una;
        uint32_t unsent = unsent_bytes(s);
        if (!unsent)
            break;
        uint32_t length = min_u32(unsent, s->mss);
        if (in_flight >= window)
        {
            /* Zero window with nothing outstanding: probe with one byte. */
            if (s->snd_wnd == 0 && in_flight == 0)
                length = 1;
            else
                break;
        }
        else
        {
            length = min_u32(length, window - in_flight);
            /* Avoid silly small segments while data is in flight. */
            if (length < min_u32(unsent, s->mss) && in_flight > 0)
                break;
        }
        uint8_t chunk[TCP_MAX_MSS];
        ring_read(s->tx_buffer, s->tx_start, s->snd_nxt - s->snd_una, chunk, length);
        bool last = (length == unsent);
        emit_segment(s, s->snd_nxt, TCP_FLAG_ACK | (last ? TCP_FLAG_PSH : 0),
                     chunk, (uint16_t)length, false);
        if (SEQ_GT(s->snd_nxt + length, s->snd_max))
            note_send_for_rtt(s, s->snd_nxt + length);
        s->snd_nxt += length;
        if (SEQ_GT(s->snd_nxt, s->snd_max))
            s->snd_max = s->snd_nxt;
        if (!s->timer_armed)
            arm_timer(s);
    }
    if (s->fin_queued && unsent_bytes(s) == 0 && !(s->fin_seq_set && SEQ_GT(s->snd_nxt, s->fin_seq)))
    {
        if (!s->fin_seq_set)
        {
            s->fin_seq_set = true;
            s->fin_seq = s->snd_nxt;
        }
        emit_segment(s, s->snd_nxt, TCP_FLAG_ACK | TCP_FLAG_FIN, 0, 0, false);
        s->snd_nxt++;
        if (SEQ_GT(s->snd_nxt, s->snd_max))
            s->snd_max = s->snd_nxt;
        if (s->state == TCP_STATE_ESTABLISHED)
            s->state = TCP_STATE_FIN_WAIT_1;
        else if (s->state == TCP_STATE_CLOSE_WAIT)
            s->state = TCP_STATE_LAST_ACK;
        if (!s->timer_armed)
            arm_timer(s);
    }
}

/* Resends the oldest unacknowledged segment without moving snd_nxt. */
static void retransmit_head(struct tcp_socket *s)
{
    stats.retransmits++;
    s->rtt_active = false;
    uint32_t flight = s->snd_nxt - s->snd_una;
    if (!flight)
        return;
    if (s->tx_length)
    {
        uint32_t length = min_u32(min_u32(s->tx_length, s->mss), flight);
        uint8_t chunk[TCP_MAX_MSS];
        ring_read(s->tx_buffer, s->tx_start, 0, chunk, length);
        emit_segment(s, s->snd_una, TCP_FLAG_ACK, chunk, (uint16_t)length, false);
    }
    else if (s->fin_seq_set)
    {
        emit_segment(s, s->fin_seq, TCP_FLAG_ACK | TCP_FLAG_FIN, 0, 0, false);
    }
}

static uint8_t parent_pending_children(int32_t parent)
{
    uint8_t count = 0;
    for (uint32_t index = 0; index < TCP_SOCKET_CAPACITY; index++)
    {
        if (sockets[index].used && sockets[index].parent == parent && sockets[index].state == TCP_STATE_SYN_RECEIVED)
            count++;
    }
    return count;
}

static void become_established(struct tcp_socket *s)
{
    s->state = TCP_STATE_ESTABLISHED;
    s->retries = 0;
    s->arp_tries = 0;
    s->timer_armed = false;
    s->cwnd = TCP_INITIAL_CWND_SEGMENTS * s->mss;
    s->ssthresh = 0xFFFFFFFFU;
    stats.connections_established++;
    if (s->parent >= 0)
    {
        struct tcp_socket *parent = &sockets[s->parent];
        if (parent->used && parent->state == TCP_STATE_LISTEN && parent->accept_count < TCP_ACCEPT_BACKLOG_MAX)
        {
            parent->accept_queue[parent->accept_count++] = (int8_t)socket_index(s);
        }
    }
}

static void start_time_wait(struct tcp_socket *s)
{
    s->state = TCP_STATE_TIME_WAIT;
    s->timer_armed = false;
    s->time_wait_deadline = now_ms() + TCP_TIME_WAIT_MS;
}

static uint32_t parse_mss(const uint8_t *segment, uint16_t header_length)
{
    uint32_t mss = TCP_DEFAULT_MSS;
    uint16_t position = TCP_HEADER_LENGTH;
    while (position < header_length)
    {
        uint8_t kind = segment[position];
        if (kind == TCP_OPTION_END)
            break;
        if (kind == TCP_OPTION_NOP)
        {
            position++;
            continue;
        }
        if (position + 1 >= header_length)
            break;
        uint8_t option_length = segment[position + 1];
        if (option_length < 2 || position + option_length > header_length)
            break;
        if (kind == TCP_OPTION_MSS && option_length == 4)
            mss = net_read_be16(segment + position + 2);
        position = (uint16_t)(position + option_length);
    }
    if (mss < 64)
        mss = TCP_DEFAULT_MSS;
    return min_u32(mss, TCP_MAX_MSS);
}

static struct tcp_socket *find_socket(uint32_t local_address,
                                      uint16_t local_port,
                                      uint32_t remote_address,
                                      uint16_t remote_port)
{
    struct tcp_socket *listener = 0;
    for (uint32_t index = 0; index < TCP_SOCKET_CAPACITY; index++)
    {
        struct tcp_socket *s = &sockets[index];
        if (!s->used || s->state == TCP_STATE_CLOSED || s->local_port != local_port)
            continue;
        if (s->state == TCP_STATE_LISTEN)
        {
            if (!s->local_address || s->local_address == local_address)
                listener = s;
            continue;
        }
        if (s->remote_port == remote_port && s->remote_address == remote_address && s->local_address == local_address)
            return s;
    }
    return listener;
}

static void handle_ack(struct tcp_socket *s, uint32_t ack, uint32_t window,
                       uint32_t sequence, bool has_data)
{
    if (SEQ_LT(s->snd_una, ack) && SEQ_LEQ(ack, s->snd_max))
    {
        uint32_t acked = ack - s->snd_una;
        bool fin_acked = s->fin_seq_set && SEQ_GT(ack, s->fin_seq);
        uint32_t data_acked = acked - (fin_acked ? 1U : 0U);
        if (data_acked > s->tx_length)
            data_acked = s->tx_length;
        s->tx_start = (s->tx_start + data_acked) % TCP_BUFFER_SIZE;
        s->tx_length -= data_acked;
        s->snd_una = ack;
        if (SEQ_LT(s->snd_nxt, ack))
            s->snd_nxt = ack;
        if (s->rtt_active && SEQ_LEQ(s->rtt_seq, ack))
        {
            rtt_sample(s, (uint32_t)(now_ms() - s->rtt_start));
            s->rtt_active = false;
        }
        if (s->in_recovery)
        {
            s->cwnd = s->ssthresh;
            s->in_recovery = false;
        }
        else if (s->cwnd < s->ssthresh)
        {
            s->cwnd += min_u32(data_acked, s->mss);
        }
        else if (s->cwnd)
        {
            s->cwnd += max_u32((s->mss * s->mss) / s->cwnd, 1U);
        }
        s->dup_acks = 0;
        s->retries = 0;
        s->arp_tries = 0;
        if (s->snd_una == s->snd_max)
            s->timer_armed = false;
        else
            arm_timer(s);
        if (fin_acked)
        {
            if (s->state == TCP_STATE_FIN_WAIT_1)
            {
                s->state = TCP_STATE_FIN_WAIT_2;
                s->time_wait_deadline = now_ms() + 2U * TCP_TIME_WAIT_MS;
            }
            else if (s->state == TCP_STATE_CLOSING)
                start_time_wait(s);
            else if (s->state == TCP_STATE_LAST_ACK)
            {
                enter_closed(s, 0);
                return;
            }
        }
    }
    else if (ack == s->snd_una && !has_data && s->snd_una != s->snd_max && window == s->snd_wnd)
    {
        s->dup_acks++;
        if (s->dup_acks == 3)
        {
            uint32_t flight = s->snd_nxt - s->snd_una;
            s->ssthresh = max_u32(flight / 2, 2U * s->mss);
            s->cwnd = s->ssthresh + 3U * s->mss;
            s->in_recovery = true;
            stats.fast_retransmits++;
            retransmit_head(s);
        }
        else if (s->dup_acks > 3 && s->in_recovery)
        {
            s->cwnd += s->mss;
        }
    }
    if (SEQ_LT(s->snd_wl1, sequence) || (s->snd_wl1 == sequence && SEQ_LEQ(s->snd_wl2, ack)))
    {
        s->snd_wnd = window;
        s->snd_wl1 = sequence;
        s->snd_wl2 = ack;
    }
}

static void process_segment(struct tcp_socket *s, const struct ipv4_packet *packet,
                            uint32_t sequence, uint32_t ack, uint8_t flags,
                            uint32_t window, const uint8_t *data,
                            uint32_t data_length, uint32_t peer_mss)
{
    bool syn = flags & TCP_FLAG_SYN, fin = flags & TCP_FLAG_FIN, rst = flags & TCP_FLAG_RST;
    bool has_ack = flags & TCP_FLAG_ACK;

    if (s->state == TCP_STATE_SYN_SENT)
    {
        if (has_ack && (SEQ_LEQ(ack, s->iss) || SEQ_GT(ack, s->snd_nxt)))
        {
            if (!rst)
                send_reset(packet->device, s->local_address, s->remote_address,
                           s->local_port, s->remote_port, ack, 0, TCP_FLAG_RST);
            return;
        }
        if (rst)
        {
            if (has_ack)
                enter_closed(s, TCP_ERR_RESET);
            return;
        }
        if (!syn)
            return;
        s->irs = sequence;
        s->rcv_nxt = sequence + 1;
        s->mss = peer_mss;
        s->snd_wnd = window;
        s->snd_wl1 = sequence;
        s->snd_wl2 = ack;
        if (has_ack)
        {
            s->snd_una = ack;
            s->rtt_active = false;
            become_established(s);
            send_ack(s);
            tcp_output(s);
        }
        else
        {
            s->state = TCP_STATE_SYN_RECEIVED;
            s->timer_armed = false;
            send_syn(s);
            arm_timer(s);
        }
        return;
    }

    if (s->state == TCP_STATE_TIME_WAIT)
    {
        if (!rst && (fin || data_length))
        {
            send_ack(s);
            s->time_wait_deadline = now_ms() + TCP_TIME_WAIT_MS;
        }
        return;
    }

    /* Acceptable segment: starts at or overlaps rcv_nxt. */
    uint32_t receive_space = receive_window(s);
    uint32_t seg_length = data_length + (fin ? 1U : 0U);
    bool acceptable;
    if (seg_length == 0)
        acceptable = (sequence == s->rcv_nxt) || (SEQ_GT(sequence, s->rcv_nxt) && SEQ_LT(sequence, s->rcv_nxt + max_u32(receive_space, 1U)));
    else
        acceptable = SEQ_LT(sequence, s->rcv_nxt + max_u32(receive_space, 1U)) && SEQ_GT(sequence + seg_length, s->rcv_nxt);
    if (rst)
    {
        if (sequence == s->rcv_nxt || (acceptable && s->state != TCP_STATE_SYN_RECEIVED))
            enter_closed(s, TCP_ERR_RESET);
        return;
    }
    if (!acceptable)
    {
        send_ack(s);
        return;
    }
    if (syn)
    {
        if (s->state == TCP_STATE_SYN_RECEIVED && sequence == s->irs)
        {
            send_syn(s);
            return;
        }
        send_reset(packet->device, s->local_address, s->remote_address,
                   s->local_port, s->remote_port, s->snd_nxt, 0, TCP_FLAG_RST);
        enter_closed(s, TCP_ERR_RESET);
        return;
    }
    if (!has_ack)
        return;

    if (s->state == TCP_STATE_SYN_RECEIVED)
    {
        if (SEQ_LT(s->snd_una, ack) && SEQ_LEQ(ack, s->snd_nxt))
        {
            s->snd_una = ack;
            s->snd_wnd = window;
            s->snd_wl1 = sequence;
            s->snd_wl2 = ack;
            s->rtt_active = false;
            become_established(s);
        }
        else
        {
            send_reset(packet->device, s->local_address, s->remote_address,
                       s->local_port, s->remote_port, ack, 0, TCP_FLAG_RST);
            return;
        }
    }

    handle_ack(s, ack, window, sequence, data_length > 0);
    if (s->state == TCP_STATE_CLOSED || !s->used)
        return;

    uint32_t fin_sequence = sequence + data_length;
    /* Trim data already received. */
    if (SEQ_LT(sequence, s->rcv_nxt))
    {
        uint32_t overlap = s->rcv_nxt - sequence;
        if (overlap >= data_length)
        {
            data += data_length;
            data_length = 0;
        }
        else
        {
            data += overlap;
            data_length -= overlap;
        }
        sequence = s->rcv_nxt;
    }

    bool need_ack = false;
    if (data_length && (s->state == TCP_STATE_ESTABLISHED || s->state == TCP_STATE_FIN_WAIT_1 || s->state == TCP_STATE_FIN_WAIT_2))
    {
        if (sequence == s->rcv_nxt)
        {
            uint32_t space = TCP_BUFFER_SIZE - s->rx_length;
            uint32_t take = min_u32(data_length, space);
            if (take)
            {
                ring_write(s->rx_buffer, s->rx_start, s->rx_length, data, take);
                s->rx_length += take;
                s->rcv_nxt += take;
            }
        }
        need_ack = true;
    }
    if (fin && fin_sequence == s->rcv_nxt && (s->state == TCP_STATE_ESTABLISHED || s->state == TCP_STATE_FIN_WAIT_1 || s->state == TCP_STATE_FIN_WAIT_2))
    {
        s->rcv_nxt++;
        s->fin_received = true;
        need_ack = true;
        if (s->state == TCP_STATE_ESTABLISHED)
            s->state = TCP_STATE_CLOSE_WAIT;
        else if (s->state == TCP_STATE_FIN_WAIT_1)
        {
            bool fin_acked = s->fin_seq_set && SEQ_GT(s->snd_una, s->fin_seq);
            if (fin_acked)
                start_time_wait(s);
            else
                s->state = TCP_STATE_CLOSING;
        }
        else
            start_time_wait(s);
    }
    tcp_output(s);
    if (need_ack)
        send_ack(s);
}

static void receive_segment(const struct ipv4_packet *packet)
{
    if (packet->payload_length < TCP_HEADER_LENGTH)
        return;
    const uint8_t *segment = packet->payload;
    uint16_t header_length = (uint16_t)((segment[12] >> 4) * 4U);
    if (header_length < TCP_HEADER_LENGTH || header_length > packet->payload_length)
        return;
    if (tcp_checksum(packet->source, packet->destination, segment,
                     packet->payload_length) != 0)
    {
        stats.bad_checksum++;
        return;
    }
    uint16_t source_port = net_read_be16(segment);
    uint16_t destination_port = net_read_be16(segment + 2);
    uint32_t sequence = net_read_be32(segment + 4);
    uint32_t ack = net_read_be32(segment + 8);
    uint8_t flags = segment[13];
    uint32_t window = net_read_be16(segment + 14);
    const uint8_t *data = segment + header_length;
    uint32_t data_length = (uint32_t)(packet->payload_length - header_length);
    uint32_t peer_mss = (flags & TCP_FLAG_SYN) ? parse_mss(segment, header_length)
                                               : TCP_DEFAULT_MSS;
    if (!destination_port || !source_port)
        return;
    stats.segments_received++;

    MUTEX_SCOPE(&tcp_lock);
    struct tcp_socket *s = find_socket(packet->destination, destination_port,
                                       packet->source, source_port);
    if (!s)
    {
        if (flags & TCP_FLAG_RST)
            return;
        if (flags & TCP_FLAG_ACK)
            send_reset(packet->device, packet->destination, packet->source,
                       destination_port, source_port, ack, 0, TCP_FLAG_RST);
        else
            send_reset(packet->device, packet->destination, packet->source,
                       destination_port, source_port, 0,
                       sequence + data_length + ((flags & TCP_FLAG_SYN) ? 1U : 0U) + ((flags & TCP_FLAG_FIN) ? 1U : 0U),
                       TCP_FLAG_RST | TCP_FLAG_ACK);
        return;
    }
    if (s->state == TCP_STATE_LISTEN)
    {
        if (flags & TCP_FLAG_RST)
            return;
        if (flags & TCP_FLAG_ACK)
        {
            send_reset(packet->device, packet->destination, packet->source,
                       destination_port, source_port, ack, 0, TCP_FLAG_RST);
            return;
        }
        if (!(flags & TCP_FLAG_SYN))
            return;
        int32_t parent = socket_index(s);
        if (s->accept_count + parent_pending_children(parent) >= s->backlog)
            return;
        struct tcp_socket *child = socket_allocate();
        if (!child)
            return;
        child->parent = (int8_t)parent;
        child->device = packet->device;
        child->local_address = packet->destination;
        child->local_port = destination_port;
        child->remote_address = packet->source;
        child->remote_port = source_port;
        child->irs = sequence;
        child->rcv_nxt = sequence + 1;
        child->mss = peer_mss;
        child->snd_wnd = window;
        child->iss = generate_isn();
        child->snd_una = child->iss;
        child->snd_nxt = child->iss + 1;
        child->snd_max = child->snd_nxt;
        child->snd_wl1 = sequence;
        child->snd_wl2 = 0;
        child->state = TCP_STATE_SYN_RECEIVED;
        send_syn(child);
        arm_timer(child);
        return;
    }
    process_segment(s, packet, sequence, ack, flags, window, data, data_length, peer_mss);
}

bool tcp_init(void)
{
    memset(sockets, 0, sizeof(sockets));
    memset(&stats, 0, sizeof(stats));
    memset(&tcp_lock, 0, sizeof(tcp_lock));
    for (uint32_t index = 0; index < TCP_SOCKET_CAPACITY; index++)
        sockets[index].parent = -1;
    next_ephemeral = TCP_EPHEMERAL_FIRST;
    isn_counter = 0;
    return ipv4_register_handler(IPV4_PROTOCOL_TCP, receive_segment);
}

static void handle_timeout(struct tcp_socket *s, uint64_t now)
{
    if (s->arp_pending && s->arp_tries < TCP_MAX_ARP_RETRIES)
    {
        /* Waiting for ARP, not a lost segment: retry without backing off. */
        s->arp_tries++;
        if (s->state == TCP_STATE_SYN_SENT || s->state == TCP_STATE_SYN_RECEIVED)
            send_syn(s);
        else
            retransmit_head(s);
        arm_timer(s);
        return;
    }
    bool handshake = (s->state == TCP_STATE_SYN_SENT || s->state == TCP_STATE_SYN_RECEIVED);
    if (s->snd_wnd == 0 && !handshake)
        s->retries = 0;
    s->retries++;
    if (s->retries > (handshake ? TCP_MAX_SYN_RETRIES : TCP_MAX_RETRIES))
    {
        stats.connections_timed_out++;
        enter_closed(s, TCP_ERR_TIMEOUT);
        return;
    }
    s->rto_ms = min_u32(s->rto_ms * 2U, TCP_MAX_RTO_MS);
    uint32_t flight = s->snd_nxt - s->snd_una;
    s->ssthresh = max_u32(flight / 2U, 2U * s->mss);
    s->cwnd = s->mss;
    s->dup_acks = 0;
    s->in_recovery = false;
    s->rtt_active = false;
    arm_timer(s);
    if (handshake)
    {
        stats.retransmits++;
        send_syn(s);
        return;
    }
    /* Go-back-N from the oldest unacknowledged byte. */
    s->snd_nxt = s->snd_una;
    stats.retransmits++;
    tcp_output(s);
    s->rtt_active = false;
}

void tcp_poll(uint64_t now)
{
    MUTEX_SCOPE(&tcp_lock);
    for (uint32_t index = 0; index < TCP_SOCKET_CAPACITY; index++)
    {
        struct tcp_socket *s = &sockets[index];
        if (!s->used)
            continue;
        if ((s->state == TCP_STATE_TIME_WAIT || (s->state == TCP_STATE_FIN_WAIT_2 && s->app_closed)) && now >= s->time_wait_deadline)
        {
            enter_closed(s, 0);
            continue;
        }
        if (s->timer_armed && now >= s->timer_deadline)
            handle_timeout(s, now);
    }
}

int32_t tcp_socket(void)
{
    MUTEX_SCOPE(&tcp_lock);
    struct tcp_socket *s = socket_allocate();
    return s ? socket_index(s) : TCP_ERR_NO_SOCKET;
}

static bool port_in_use(uint16_t port)
{
    for (uint32_t index = 0; index < TCP_SOCKET_CAPACITY; index++)
    {
        if (sockets[index].used && sockets[index].local_port == port && sockets[index].state != TCP_STATE_CLOSED)
            return true;
    }
    return false;
}

static uint16_t allocate_ephemeral_port(void)
{
    for (uint32_t tries = 0; tries <= TCP_EPHEMERAL_LAST - TCP_EPHEMERAL_FIRST; tries++)
    {
        uint16_t port = next_ephemeral++;
        if (next_ephemeral > TCP_EPHEMERAL_LAST || next_ephemeral < TCP_EPHEMERAL_FIRST)
            next_ephemeral = TCP_EPHEMERAL_FIRST;
        if (!port_in_use(port))
            return port;
    }
    return 0;
}

static bool pick_route(uint32_t destination, struct net_device **device,
                       uint32_t *source)
{
    struct net_device *fallback = 0;
    uint32_t fallback_source = 0;
    for (uint32_t index = 0; index < net_device_count(); index++)
    {
        struct net_device *candidate = net_device_get(index);
        struct ipv4_interface_config config;
        if (!candidate || !ipv4_get_config(candidate, &config))
            continue;
        if ((destination & config.netmask) == (config.address & config.netmask))
        {
            *device = candidate;
            *source = config.address;
            return true;
        }
        if (config.gateway && !fallback)
        {
            fallback = candidate;
            fallback_source = config.address;
        }
    }
    if (!fallback)
        return false;
    *device = fallback;
    *source = fallback_source;
    return true;
}

static uint64_t deadline_for(uint32_t timeout_ms)
{
    return timeout_ms == TCP_TIMEOUT_FOREVER ? UINT64_MAX : now_ms() + timeout_ms;
}

static bool deadline_passed(uint64_t deadline)
{
    return deadline != UINT64_MAX && now_ms() >= deadline;
}

/* Sleeps without the lock; caller re-locks and must re-validate the socket. */
static void wait_step(void)
{
    mutex_unlock(&tcp_lock);
    scheduler_sleep(1);
    mutex_lock(&tcp_lock);
}

int32_t tcp_connect(int32_t handle, uint32_t address, uint16_t port,
                    uint32_t timeout_ms)
{
    if (!address || !port)
        return TCP_ERR_INVALID;
    mutex_lock(&tcp_lock);
    struct tcp_socket *s = socket_from_handle(handle);
    if (!s || s->state != TCP_STATE_CLOSED || s->app_closed)
    {
        mutex_unlock(&tcp_lock);
        return TCP_ERR_INVALID;
    }
    struct net_device *device;
    uint32_t source;
    if (!pick_route(address, &device, &source))
    {
        mutex_unlock(&tcp_lock);
        return TCP_ERR_NO_ROUTE;
    }
    uint16_t local_port = s->local_port ? s->local_port : allocate_ephemeral_port();
    if (!local_port)
    {
        mutex_unlock(&tcp_lock);
        return TCP_ERR_IN_USE;
    }
    s->device = device;
    s->local_address = source;
    s->local_port = local_port;
    s->remote_address = address;
    s->remote_port = port;
    s->error = 0;
    s->fin_received = false;
    s->mss = TCP_MAX_MSS;
    s->iss = generate_isn();
    s->snd_una = s->iss;
    s->snd_nxt = s->iss + 1;
    s->snd_max = s->snd_nxt;
    s->state = TCP_STATE_SYN_SENT;
    send_syn(s);
    arm_timer(s);
    uint64_t deadline = deadline_for(timeout_ms);
    int32_t result = TCP_OK;
    while (s->used && s->state != TCP_STATE_ESTABLISHED)
    {
        if (s->state == TCP_STATE_CLOSED)
        {
            result = s->error ? s->error : TCP_ERR_RESET;
            break;
        }
        if (timeout_ms == TCP_TIMEOUT_NONE || deadline_passed(deadline))
        {
            result = TCP_ERR_WOULD_BLOCK;
            break;
        }
        wait_step();
    }
    if (!s->used)
        result = TCP_ERR_INVALID;
    mutex_unlock(&tcp_lock);
    return result;
}

int32_t tcp_listen(int32_t handle, uint16_t port, uint8_t backlog)
{
    if (!port)
        return TCP_ERR_INVALID;
    MUTEX_SCOPE(&tcp_lock);
    struct tcp_socket *s = socket_from_handle(handle);
    if (!s || s->state != TCP_STATE_CLOSED || s->app_closed)
        return TCP_ERR_INVALID;
    if (port_in_use(port))
        return TCP_ERR_IN_USE;
    if (!backlog)
        backlog = 1;
    if (backlog > TCP_ACCEPT_BACKLOG_MAX)
        backlog = TCP_ACCEPT_BACKLOG_MAX;
    s->local_port = port;
    s->local_address = 0;
    s->backlog = backlog;
    s->state = TCP_STATE_LISTEN;
    return TCP_OK;
}

int32_t tcp_accept(int32_t handle, uint32_t timeout_ms)
{
    mutex_lock(&tcp_lock);
    struct tcp_socket *s = socket_from_handle(handle);
    if (!s || s->state != TCP_STATE_LISTEN)
    {
        mutex_unlock(&tcp_lock);
        return TCP_ERR_INVALID;
    }
    uint64_t deadline = deadline_for(timeout_ms);
    int32_t result;
    for (;;)
    {
        if (!s->used || s->state != TCP_STATE_LISTEN)
        {
            result = TCP_ERR_INVALID;
            break;
        }
        if (s->accept_count)
        {
            result = s->accept_queue[0];
            for (uint8_t index = 1; index < s->accept_count; index++)
                s->accept_queue[index - 1] = s->accept_queue[index];
            s->accept_count--;
            sockets[result].parent = -1;
            break;
        }
        if (timeout_ms == TCP_TIMEOUT_NONE || deadline_passed(deadline))
        {
            result = TCP_ERR_WOULD_BLOCK;
            break;
        }
        wait_step();
    }
    mutex_unlock(&tcp_lock);
    return result;
}

static bool can_send_state(const struct tcp_socket *s)
{
    return s->state == TCP_STATE_ESTABLISHED || s->state == TCP_STATE_CLOSE_WAIT;
}

int32_t tcp_send(int32_t handle, const void *data, uint32_t length,
                 uint32_t timeout_ms)
{
    if (length && !data)
        return TCP_ERR_INVALID;
    mutex_lock(&tcp_lock);
    struct tcp_socket *s = socket_from_handle(handle);
    if (!s || s->app_closed)
    {
        mutex_unlock(&tcp_lock);
        return TCP_ERR_INVALID;
    }
    uint64_t deadline = deadline_for(timeout_ms);
    int32_t result;
    for (;;)
    {
        if (!s->used)
        {
            result = TCP_ERR_INVALID;
            break;
        }
        if (s->state == TCP_STATE_CLOSED)
        {
            result = s->error ? s->error : TCP_ERR_NOT_CONNECTED;
            break;
        }
        if (s->fin_queued || !(can_send_state(s) || s->state == TCP_STATE_SYN_SENT || s->state == TCP_STATE_SYN_RECEIVED))
        {
            result = TCP_ERR_NOT_CONNECTED;
            break;
        }
        if (can_send_state(s))
        {
            uint32_t space = TCP_BUFFER_SIZE - s->tx_length;
            if (!length)
            {
                result = 0;
                break;
            }
            if (space)
            {
                uint32_t take = min_u32(length, space);
                ring_write(s->tx_buffer, s->tx_start, s->tx_length,
                           (const uint8_t *)data, take);
                s->tx_length += take;
                tcp_output(s);
                result = (int32_t)take;
                break;
            }
        }
        if (timeout_ms == TCP_TIMEOUT_NONE || deadline_passed(deadline))
        {
            result = TCP_ERR_WOULD_BLOCK;
            break;
        }
        wait_step();
    }
    mutex_unlock(&tcp_lock);
    return result;
}

int32_t tcp_receive(int32_t handle, void *data, uint32_t length,
                    uint32_t timeout_ms)
{
    if (length && !data)
        return TCP_ERR_INVALID;
    mutex_lock(&tcp_lock);
    struct tcp_socket *s = socket_from_handle(handle);
    if (!s || s->app_closed || s->state == TCP_STATE_LISTEN)
    {
        mutex_unlock(&tcp_lock);
        return TCP_ERR_INVALID;
    }
    uint64_t deadline = deadline_for(timeout_ms);
    int32_t result;
    for (;;)
    {
        if (!s->used)
        {
            result = TCP_ERR_INVALID;
            break;
        }
        if (s->rx_length)
        {
            uint32_t window_before = receive_window(s);
            uint32_t take = min_u32(length, s->rx_length);
            ring_read(s->rx_buffer, s->rx_start, 0, (uint8_t *)data, take);
            s->rx_start = (s->rx_start + take) % TCP_BUFFER_SIZE;
            s->rx_length -= take;
            /* Tell the peer when a closed/small window has opened up. */
            if (s->state != TCP_STATE_CLOSED && s->state != TCP_STATE_TIME_WAIT && (window_before < s->mss || receive_window(s) - window_before >= s->mss * 2U))
                send_ack(s);
            result = (int32_t)take;
            break;
        }
        if (s->error)
        {
            result = s->error;
            break;
        }
        if (s->fin_received)
        {
            result = 0;
            break;
        }
        if (s->state == TCP_STATE_CLOSED)
        {
            result = TCP_ERR_NOT_CONNECTED;
            break;
        }
        if (s->state == TCP_STATE_SYN_SENT || s->state == TCP_STATE_SYN_RECEIVED)
        {
            /* fall through to waiting */
        }
        else if (s->state != TCP_STATE_ESTABLISHED && s->state != TCP_STATE_FIN_WAIT_1 && s->state != TCP_STATE_FIN_WAIT_2)
        {
            result = 0;
            break;
        }
        if (timeout_ms == TCP_TIMEOUT_NONE || deadline_passed(deadline))
        {
            result = TCP_ERR_WOULD_BLOCK;
            break;
        }
        wait_step();
    }
    mutex_unlock(&tcp_lock);
    return result;
}

static void abort_children(int32_t parent)
{
    for (uint32_t index = 0; index < TCP_SOCKET_CAPACITY; index++)
    {
        struct tcp_socket *child = &sockets[index];
        if (!child->used || child->parent != parent)
            continue;
        if (child->state != TCP_STATE_CLOSED && child->state != TCP_STATE_LISTEN)
            send_reset(child->device, child->local_address, child->remote_address,
                       child->local_port, child->remote_port, child->snd_nxt, 0,
                       TCP_FLAG_RST);
        socket_release(child);
    }
}

int32_t tcp_close(int32_t handle)
{
    MUTEX_SCOPE(&tcp_lock);
    struct tcp_socket *s = socket_from_handle(handle);
    if (!s || s->app_closed)
        return TCP_ERR_INVALID;
    s->app_closed = true;
    switch (s->state)
    {
    case TCP_STATE_LISTEN:
        abort_children(socket_index(s));
        socket_release(s);
        break;
    case TCP_STATE_CLOSED:
    case TCP_STATE_SYN_SENT:
        socket_release(s);
        break;
    case TCP_STATE_SYN_RECEIVED:
    case TCP_STATE_ESTABLISHED:
    case TCP_STATE_CLOSE_WAIT:
        s->fin_queued = true;
        tcp_output(s);
        break;
    default:
        break;
    }
    return TCP_OK;
}

int32_t tcp_abort(int32_t handle)
{
    MUTEX_SCOPE(&tcp_lock);
    struct tcp_socket *s = socket_from_handle(handle);
    if (!s)
        return TCP_ERR_INVALID;
    if (s->state == TCP_STATE_LISTEN)
        abort_children(socket_index(s));
    else if (s->state != TCP_STATE_CLOSED && s->state != TCP_STATE_SYN_SENT && s->state != TCP_STATE_TIME_WAIT)
        send_reset(s->device, s->local_address, s->remote_address, s->local_port,
                   s->remote_port, s->snd_nxt, 0, TCP_FLAG_RST);
    socket_release(s);
    return TCP_OK;
}

bool tcp_get_info(int32_t handle, struct tcp_socket_info *info)
{
    if (!info)
        return false;
    MUTEX_SCOPE(&tcp_lock);
    struct tcp_socket *s = socket_from_handle(handle);
    if (!s)
        return false;
    info->state = s->state;
    info->local_address = s->local_address;
    info->remote_address = s->remote_address;
    info->local_port = s->local_port;
    info->remote_port = s->remote_port;
    info->bytes_unread = s->rx_length;
    info->bytes_unsent = s->tx_length;
    info->cwnd = s->cwnd;
    info->rto_ms = s->rto_ms;
    return true;
}

void tcp_get_stats(struct tcp_stats *out)
{
    if (!out)
        return;
    MUTEX_SCOPE(&tcp_lock);
    *out = stats;
}
