#include "socket.h"
#include "../../kernel/process/scheduler.h"
#include "../../kernel/process/signal.h"
#include "../../kernel/sync/mutex.h"
#include "../../kernel/syscall/syscall.h"
#include "../../drivers/interrupts/timer.h"
#include "../../lib/string.h"
#include "../config/dhcp.h"
#include "../core/net_device.h"
#include "../name/dns.h"
#include "../transport/tcp.h"

#define SOCK_CAPACITY 64
#define SOCK_MAX_TIMEOUT_MS 0xFFFFFFF0ULL

struct sock {
    bool used;
    bool connecting;
    bool connect_failed;
    bool shut_read;
    bool shut_write;
    int32_t tcp;
    uint32_t receive_timeout_ms;
    uint32_t send_timeout_ms;
    uint32_t options;
};

static struct sock table[SOCK_CAPACITY];
static mutex_t table_mutex;

static bool lookup(int32_t id, struct sock *out)
{
    mutex_lock(&table_mutex);
    bool ok = id >= 0 && id < SOCK_CAPACITY && table[id].used;
    if (ok)
        *out = table[id];
    mutex_unlock(&table_mutex);
    return ok;
}

static void update(int32_t id, bool connecting, bool failed)
{
    mutex_lock(&table_mutex);
    if (id >= 0 && id < SOCK_CAPACITY && table[id].used)
    {
        table[id].connecting = connecting;
        table[id].connect_failed = failed;
    }
    mutex_unlock(&table_mutex);
}

static int32_t map_error(int32_t code, bool connecting, bool writing)
{
    switch (code)
    {
    case TCP_ERR_INVALID:
        return -KERR_INVAL;
    case TCP_ERR_NO_SOCKET:
        return -KERR_NOBUFS;
    case TCP_ERR_NOT_CONNECTED:
        return writing ? -KERR_PIPE : -KERR_NOTCONN;
    case TCP_ERR_RESET:
        if (connecting)
            return -KERR_CONNREFUSED;
        return writing ? -KERR_PIPE : -KERR_CONNRESET;
    case TCP_ERR_TIMEOUT:
        return -KERR_TIMEDOUT;
    case TCP_ERR_NO_ROUTE:
        return -KERR_NETUNREACH;
    case TCP_ERR_IN_USE:
        return -KERR_ADDRINUSE;
    case TCP_ERR_WOULD_BLOCK:
        return -KERR_AGAIN;
    default:
        return -KERR_IO;
    }
}

int32_t sock_create(int32_t domain, int32_t type, int32_t protocol)
{
    if (domain != SOCK_AF_INET)
        return -KERR_AFNOSUPPORT;
    if ((type & SOCK_TYPE_MASK) != SOCK_TYPE_STREAM)
        return -KERR_PROTOTYPE;
    if (protocol != 0 && protocol != SOCK_IPPROTO_TCP)
        return -KERR_PROTONOSUPPORT;
    int32_t handle = tcp_socket();
    if (handle < 0)
        return -KERR_NOBUFS;
    mutex_lock(&table_mutex);
    for (int32_t i = 0; i < SOCK_CAPACITY; i++)
    {
        if (table[i].used)
            continue;
        memset(&table[i], 0, sizeof(table[i]));
        table[i].used = true;
        table[i].tcp = handle;
        mutex_unlock(&table_mutex);
        return i;
    }
    mutex_unlock(&table_mutex);
    (void)tcp_abort(handle);
    return -KERR_NFILE;
}

void sock_release(int32_t id)
{
    struct sock s;
    mutex_lock(&table_mutex);
    bool ok = id >= 0 && id < SOCK_CAPACITY && table[id].used;
    if (ok)
    {
        s = table[id];
        table[id].used = false;
    }
    mutex_unlock(&table_mutex);
    if (ok)
        (void)tcp_close(s.tcp);
}

static uint32_t timeout_or_forever(uint32_t value)
{
    return value ? value : TCP_TIMEOUT_FOREVER;
}

static int32_t wait_for(int32_t tcp, uint32_t mask, bool nonblock,
                        uint32_t timeout_ms)
{
    uint64_t started = timer_ticks();
    for (;;)
    {
        uint32_t ready = tcp_readiness(tcp);
        if (ready & mask)
            return 0;
        if (nonblock)
            return -KERR_AGAIN;
        if (timeout_ms != TCP_TIMEOUT_FOREVER &&
            timer_ticks() - started >= timeout_ms)
            return -KERR_AGAIN;
        if (signal_pending_current())
            return -KERR_INTR;
        scheduler_sleep(1);
    }
}

int32_t sock_connect(int32_t id, const struct sock_addr_in *address,
                     bool nonblock)
{
    struct sock s;
    if (!lookup(id, &s))
        return -KERR_BADF;
    if (address->family != SOCK_AF_INET)
        return -KERR_AFNOSUPPORT;
    if (s.connecting)
    {
        struct tcp_socket_info info;
        if (!tcp_get_info(s.tcp, &info))
            return -KERR_BADF;
        if (info.state == TCP_STATE_ESTABLISHED)
        {
            update(id, false, false);
            return -KERR_ISCONN;
        }
        if (info.state == TCP_STATE_CLOSED)
        {
            int32_t err = tcp_pending_error(s.tcp);
            update(id, false, true);
            return map_error(err ? err : TCP_ERR_RESET, true, false);
        }
        if (nonblock)
            return -KERR_ALREADY;
    }
    else
    {
        uint32_t host = __builtin_bswap32(address->address_be);
        uint16_t port = __builtin_bswap16(address->port_be);
        if (!host || !port)
            return -KERR_ADDRNOTAVAIL;
        struct tcp_socket_info info;
        if (tcp_get_info(s.tcp, &info) && info.state == TCP_STATE_ESTABLISHED)
            return -KERR_ISCONN;
        int32_t status = tcp_connect(s.tcp, host, port, TCP_TIMEOUT_NONE);
        if (status != TCP_OK && status != TCP_ERR_WOULD_BLOCK)
            return map_error(status, true, false);
        if (status == TCP_OK)
            return 0;
        update(id, true, false);
        if (nonblock)
            return -KERR_INPROGRESS;
    }
    int32_t waited = wait_for(s.tcp, TCP_READY_OUT | TCP_READY_ERR | TCP_READY_HUP,
                              false, timeout_or_forever(s.send_timeout_ms));
    if (waited == -KERR_AGAIN)
        return -KERR_TIMEDOUT;
    if (waited < 0)
        return waited;
    struct tcp_socket_info info;
    if (!tcp_get_info(s.tcp, &info))
        return -KERR_BADF;
    if (info.state == TCP_STATE_ESTABLISHED)
    {
        update(id, false, false);
        return 0;
    }
    int32_t err = tcp_pending_error(s.tcp);
    update(id, false, true);
    return map_error(err ? err : TCP_ERR_RESET, true, false);
}

int32_t sock_bind(int32_t id, const struct sock_addr_in *address)
{
    struct sock s;
    if (!lookup(id, &s))
        return -KERR_BADF;
    if (address->family != SOCK_AF_INET)
        return -KERR_AFNOSUPPORT;
    int32_t status = tcp_bind(s.tcp, __builtin_bswap16(address->port_be));
    return status == TCP_OK ? 0 : map_error(status, false, false);
}

int32_t sock_listen(int32_t id, int32_t backlog)
{
    struct sock s;
    if (!lookup(id, &s))
        return -KERR_BADF;
    struct tcp_socket_info info;
    if (!tcp_get_info(s.tcp, &info))
        return -KERR_BADF;
    if (!info.local_port)
        return -KERR_DESTADDRREQ;
    if (backlog < 1)
        backlog = 1;
    int32_t status = tcp_listen(s.tcp, info.local_port, (uint8_t)backlog);
    return status == TCP_OK ? 0 : map_error(status, false, false);
}

int32_t sock_accept(int32_t id, bool nonblock, int32_t *new_id,
                    struct sock_addr_in *peer)
{
    struct sock s;
    if (!lookup(id, &s))
        return -KERR_BADF;
    int32_t waited = wait_for(s.tcp, TCP_READY_IN | TCP_READY_ERR | TCP_READY_HUP,
                              nonblock, TCP_TIMEOUT_FOREVER);
    if (waited < 0)
        return waited;
    int32_t child = tcp_accept(s.tcp, TCP_TIMEOUT_NONE);
    if (child < 0)
        return map_error(child, false, false);
    mutex_lock(&table_mutex);
    int32_t slot = -1;
    for (int32_t i = 0; i < SOCK_CAPACITY; i++)
    {
        if (!table[i].used)
        {
            slot = i;
            memset(&table[i], 0, sizeof(table[i]));
            table[i].used = true;
            table[i].tcp = child;
            break;
        }
    }
    mutex_unlock(&table_mutex);
    if (slot < 0)
    {
        (void)tcp_abort(child);
        return -KERR_NFILE;
    }
    if (peer)
    {
        struct tcp_socket_info info;
        memset(peer, 0, sizeof(*peer));
        peer->family = SOCK_AF_INET;
        if (tcp_get_info(child, &info))
        {
            peer->port_be = __builtin_bswap16(info.remote_port);
            peer->address_be = __builtin_bswap32(info.remote_address);
        }
    }
    *new_id = slot;
    return 0;
}

int32_t sock_read(int32_t id, void *buffer, uint32_t count, bool nonblock,
                  uint32_t flags)
{
    struct sock s;
    if (!lookup(id, &s))
        return -KERR_BADF;
    if (s.shut_read)
        return 0;
    if (!count)
        return 0;
    if (flags & SOCK_MSG_DONTWAIT)
        nonblock = true;
    int32_t waited = wait_for(s.tcp, TCP_READY_IN | TCP_READY_ERR | TCP_READY_HUP,
                              nonblock, timeout_or_forever(s.receive_timeout_ms));
    if (waited < 0)
        return waited;
    int32_t got = tcp_receive(s.tcp, buffer, count, TCP_TIMEOUT_NONE);
    if (got == TCP_ERR_NOT_CONNECTED)
        return 0;
    return got < 0 ? map_error(got, false, false) : got;
}

int32_t sock_write(int32_t id, const void *buffer, uint32_t count,
                   bool nonblock, uint32_t flags)
{
    struct sock s;
    if (!lookup(id, &s))
        return -KERR_BADF;
    if (s.shut_write)
        return -KERR_PIPE;
    if (!count)
        return 0;
    if (flags & SOCK_MSG_DONTWAIT)
        nonblock = true;
    int32_t waited = wait_for(s.tcp, TCP_READY_OUT | TCP_READY_ERR | TCP_READY_HUP,
                              nonblock, timeout_or_forever(s.send_timeout_ms));
    if (waited < 0)
        return waited;
    int32_t sent = tcp_send(s.tcp, buffer, count, TCP_TIMEOUT_NONE);
    return sent < 0 ? map_error(sent, false, true) : sent;
}

int32_t sock_shutdown(int32_t id, int32_t how)
{
    struct sock s;
    if (!lookup(id, &s))
        return -KERR_BADF;
    if (how < 0 || how > 2)
        return -KERR_INVAL;
    struct tcp_socket_info info;
    if (!tcp_get_info(s.tcp, &info))
        return -KERR_BADF;
    if (info.state == TCP_STATE_CLOSED || info.state == TCP_STATE_LISTEN ||
        info.state == TCP_STATE_SYN_SENT)
        return -KERR_NOTCONN;
    mutex_lock(&table_mutex);
    if (how == 0 || how == 2)
        table[id].shut_read = true;
    if (how == 1 || how == 2)
        table[id].shut_write = true;
    mutex_unlock(&table_mutex);
    if (how == 1 || how == 2)
        (void)tcp_shutdown_write(s.tcp);
    return 0;
}

int32_t sock_poll(int32_t id)
{
    struct sock s;
    if (!lookup(id, &s))
        return KPOLLNVAL;
    uint32_t ready = tcp_readiness(s.tcp);
    int32_t out = 0;
    if (ready & TCP_READY_IN)
        out |= KPOLLIN;
    if (ready & TCP_READY_OUT)
        out |= KPOLLOUT;
    if (ready & TCP_READY_ERR)
        out |= KPOLLERR;
    if (ready & TCP_READY_HUP)
        out |= KPOLLHUP;
    if (s.shut_read)
        out |= KPOLLIN;
    return out;
}

int32_t sock_name(int32_t id, bool peer, struct sock_addr_in *out)
{
    struct sock s;
    if (!lookup(id, &s))
        return -KERR_BADF;
    struct tcp_socket_info info;
    if (!tcp_get_info(s.tcp, &info))
        return -KERR_BADF;
    memset(out, 0, sizeof(*out));
    out->family = SOCK_AF_INET;
    if (peer)
    {
        if (info.state == TCP_STATE_CLOSED || info.state == TCP_STATE_LISTEN ||
            info.state == TCP_STATE_SYN_SENT)
            return -KERR_NOTCONN;
        out->port_be = __builtin_bswap16(info.remote_port);
        out->address_be = __builtin_bswap32(info.remote_address);
    }
    else
    {
        out->port_be = __builtin_bswap16(info.local_port);
        out->address_be = __builtin_bswap32(info.local_address);
    }
    return 0;
}

static uint32_t timeval_to_ms(const struct sock_timeval *tv)
{
    if (tv->seconds < 0 || tv->microseconds < 0)
        return 0;
    uint64_t ms = (uint64_t)tv->seconds * 1000ULL +
                  (uint64_t)(tv->microseconds + 999) / 1000ULL;
    return ms > SOCK_MAX_TIMEOUT_MS ? (uint32_t)SOCK_MAX_TIMEOUT_MS : (uint32_t)ms;
}

int32_t sock_setopt(int32_t id, int32_t level, int32_t name, const void *value,
                    uint32_t length)
{
    struct sock s;
    if (!lookup(id, &s))
        return -KERR_BADF;
    if (level == SOCK_SOL_SOCKET)
    {
        switch (name)
        {
        case SOCK_SO_RCVTIMEO:
        case SOCK_SO_SNDTIMEO:
        {
            if (length < sizeof(struct sock_timeval))
                return -KERR_INVAL;
            uint32_t ms = timeval_to_ms((const struct sock_timeval *)value);
            mutex_lock(&table_mutex);
            if (name == SOCK_SO_RCVTIMEO)
                table[id].receive_timeout_ms = ms;
            else
                table[id].send_timeout_ms = ms;
            mutex_unlock(&table_mutex);
            return 0;
        }
        case SOCK_SO_REUSEADDR:
        case SOCK_SO_KEEPALIVE:
        case SOCK_SO_SNDBUF:
        case SOCK_SO_RCVBUF:
        case SOCK_SO_LINGER:
            return 0;
        default:
            return -KERR_NOPROTOOPT;
        }
    }
    if (level == SOCK_IPPROTO_TCP && name == SOCK_TCP_NODELAY)
        return 0;
    return -KERR_NOPROTOOPT;
}

int32_t sock_getopt(int32_t id, int32_t level, int32_t name, void *value,
                    uint32_t *length)
{
    struct sock s;
    if (!lookup(id, &s))
        return -KERR_BADF;
    if (level != SOCK_SOL_SOCKET)
        return -KERR_NOPROTOOPT;
    if (name == SOCK_SO_RCVTIMEO || name == SOCK_SO_SNDTIMEO)
    {
        if (*length < sizeof(struct sock_timeval))
            return -KERR_INVAL;
        uint32_t ms = name == SOCK_SO_RCVTIMEO ? s.receive_timeout_ms
                                               : s.send_timeout_ms;
        struct sock_timeval *tv = value;
        tv->seconds = ms / 1000U;
        tv->microseconds = (int64_t)(ms % 1000U) * 1000;
        *length = sizeof(*tv);
        return 0;
    }
    if (*length < sizeof(int32_t))
        return -KERR_INVAL;
    int32_t result;
    switch (name)
    {
    case SOCK_SO_ERROR:
    {
        struct tcp_socket_info info;
        result = 0;
        if (tcp_get_info(s.tcp, &info) && info.state == TCP_STATE_CLOSED &&
            (s.connecting || s.connect_failed))
        {
            int32_t err = tcp_pending_error(s.tcp);
            result = -map_error(err ? err : TCP_ERR_RESET, true, false);
        }
        else if (s.connecting && tcp_get_info(s.tcp, &info) &&
                 info.state == TCP_STATE_ESTABLISHED)
            update(id, false, false);
        break;
    }
    case SOCK_SO_TYPE:
        result = SOCK_TYPE_STREAM;
        break;
    case SOCK_SO_SNDBUF:
    case SOCK_SO_RCVBUF:
        result = TCP_BUFFER_SIZE;
        break;
    case SOCK_SO_REUSEADDR:
    case SOCK_SO_KEEPALIVE:
        result = 0;
        break;
    default:
        return -KERR_NOPROTOOPT;
    }
    *(int32_t *)value = result;
    *length = sizeof(int32_t);
    return 0;
}

int32_t sock_resolve(const char *name, uint32_t timeout_ms,
                     uint32_t *address_be)
{
    struct net_device *device = net_device_default();
    if (!device)
        return -KERR_NETUNREACH;
    if (timeout_ms < 100)
        timeout_ms = 100;
    uint64_t started = timer_ticks();
    for (;;)
    {
        uint32_t address = 0;
        enum dns_result result = dns_resolve_ipv4(device, name, timeout_ms, &address);
        if (result == DNS_RESULT_OK)
        {
            *address_be = __builtin_bswap32(address);
            return 0;
        }
        if (result == DNS_RESULT_BUSY)
        {
            if (timer_ticks() - started >= timeout_ms)
                return -KERR_TIMEDOUT;
            if (signal_pending_current())
                return -KERR_INTR;
            scheduler_sleep(10);
            continue;
        }
        switch (result)
        {
        case DNS_RESULT_INVALID:
            return -KERR_INVAL;
        case DNS_RESULT_NOT_FOUND:
            return -KERR_NOENT;
        case DNS_RESULT_TIMEOUT:
            return -KERR_TIMEDOUT;
        case DNS_RESULT_NO_SERVER:
            return -KERR_NETUNREACH;
        default:
            return -KERR_IO;
        }
    }
}
