#include "../../libc/include/purec.h"
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <stdio.h>
#include <syslog.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define CLIENT_PORT 68
#define SERVER_PORT 67
#define MAGIC 0x63825363U
#define PACKET_MIN 300

enum { DISCOVER = 1, OFFER, REQUEST, DECLINE, ACK, NAK, RELEASE };
enum state { INIT, SELECTING, REQUESTING, BOUND, RENEWING, REBINDING };

struct lease {
    uint32_t address;
    uint32_t netmask;
    uint32_t router;
    uint32_t dns[3];
    uint32_t dns_count;
    uint32_t server;
    uint32_t lease_s;
    uint32_t t1_s;
    uint32_t t2_s;
};

static const char *ifname;
static uint8_t mac[6];
static int sock = -1;
static uint32_t xid;
static enum state state = INIT;
static struct lease offer, bound;
static uint64_t bound_at, last_send, next_send;
static unsigned attempt;
static volatile sig_atomic_t stop;

static uint64_t now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000ULL + (uint64_t)t.tv_nsec / 1000000ULL;
}

static void ip_text(uint32_t a, char *out)
{
    sprintf(out, "%u.%u.%u.%u", (a >> 24) & 255, (a >> 16) & 255, (a >> 8) & 255, a & 255);
}

static void say(const char *fmt, ...)
{
    char text[200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);
    syslog(LOG_INFO, "%s: %s", ifname, text);
}

static bool iface(struct net_if_info *out)
{
    struct net_if_info list[NET_IF_MAX_COUNT];
    int32_t n = pc_net_if_list(list, NET_IF_MAX_COUNT);
    for (int32_t i = 0; i < n; i++)
        if (strcmp(list[i].name, ifname) == 0) {
            *out = list[i];
            return true;
        }
    return false;
}

static int net_cfg(uint32_t op, uint32_t a, uint32_t m, uint32_t g, uint32_t d)
{
    struct net_if_config c;
    memset(&c, 0, sizeof(c));
    snprintf(c.name, sizeof(c.name), "%s", ifname);
    c.op = op;
    c.address = a;
    c.netmask = m;
    c.gateway = g;
    c.dns = d;
    return pc_net_configure(&c);
}

static size_t opt(uint8_t *p, size_t at, uint8_t code, const void *data, uint8_t len)
{
    p[at++] = code;
    p[at++] = len;
    memcpy(p + at, data, len);
    return at + len;
}

static size_t opt32(uint8_t *p, size_t at, uint8_t code, uint32_t v)
{
    uint8_t b[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v};
    return opt(p, at, code, b, 4);
}

static bool send_msg(uint8_t type, uint32_t ciaddr, uint32_t requested, uint32_t server,
                     uint32_t to)
{
    uint8_t p[548];
    memset(p, 0, sizeof(p));
    p[0] = 1;
    p[1] = 1;
    p[2] = 6;
    p[4] = (uint8_t)(xid >> 24);
    p[5] = (uint8_t)(xid >> 16);
    p[6] = (uint8_t)(xid >> 8);
    p[7] = (uint8_t)xid;
    if (!ciaddr)
        p[10] = 0x80;
    p[12] = (uint8_t)(ciaddr >> 24);
    p[13] = (uint8_t)(ciaddr >> 16);
    p[14] = (uint8_t)(ciaddr >> 8);
    p[15] = (uint8_t)ciaddr;
    memcpy(p + 28, mac, 6);
    p[236] = 0x63;
    p[237] = 0x82;
    p[238] = 0x53;
    p[239] = 0x63;
    size_t at = 240;
    at = opt(p, at, 53, &type, 1);
    uint8_t client_id[7] = {1, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]};
    at = opt(p, at, 61, client_id, 7);
    if (requested)
        at = opt32(p, at, 50, requested);
    if (server)
        at = opt32(p, at, 54, server);
    char host[64] = "";
    FILE *f = fopen("/etc/hostname", "r");
    if (f) {
        if (fgets(host, sizeof(host), f)) {
            char *nl = strchr(host, '\n');
            if (nl)
                *nl = 0;
        }
        fclose(f);
    }
    if (host[0])
        at = opt(p, at, 12, host, (uint8_t)strlen(host));
    if (type != RELEASE) {
        static const uint8_t params[] = {1, 3, 6, 15, 51, 54, 58, 59};
        at = opt(p, at, 55, params, sizeof(params));
    }
    p[at++] = 255;
    if (at < PACKET_MIN)
        at = PACKET_MIN;
    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port = htons(SERVER_PORT);
    dst.sin_addr.s_addr = htonl(to);
    last_send = now_ms();
    return sendto(sock, p, at, 0, (struct sockaddr *)&dst, sizeof(dst)) == (ssize_t)at;
}

static uint32_t rd32(const uint8_t *b)
{
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
}

static int parse(const uint8_t *p, ssize_t len, struct lease *out)
{
    if (len < 240 || p[0] != 2 || rd32(p + 4) != xid || memcmp(p + 28, mac, 6) != 0 ||
        rd32(p + 236) != MAGIC)
        return 0;
    memset(out, 0, sizeof(*out));
    out->address = rd32(p + 16);
    int type = 0;
    for (ssize_t i = 240; i < len;) {
        uint8_t code = p[i++];
        if (code == 0)
            continue;
        if (code == 255 || i >= len)
            break;
        uint8_t l = p[i++];
        if (i + l > len)
            break;
        const uint8_t *v = p + i;
        if (code == 53 && l >= 1)
            type = v[0];
        else if (code == 1 && l >= 4)
            out->netmask = rd32(v);
        else if (code == 3 && l >= 4)
            out->router = rd32(v);
        else if (code == 6)
            for (uint8_t k = 0; k + 4 <= l && out->dns_count < 3; k += 4)
                out->dns[out->dns_count++] = rd32(v + k);
        else if (code == 51 && l >= 4)
            out->lease_s = rd32(v);
        else if (code == 54 && l >= 4)
            out->server = rd32(v);
        else if (code == 58 && l >= 4)
            out->t1_s = rd32(v);
        else if (code == 59 && l >= 4)
            out->t2_s = rd32(v);
        i += l;
    }
    if (!out->netmask)
        out->netmask = 0xFFFFFF00U;
    if (!out->lease_s)
        out->lease_s = 0xFFFFFFFFU;
    if (!out->t1_s)
        out->t1_s = out->lease_s == 0xFFFFFFFFU ? 0xFFFFFFFFU : out->lease_s / 2;
    if (!out->t2_s)
        out->t2_s = out->lease_s == 0xFFFFFFFFU ? 0xFFFFFFFFU : out->lease_s / 8 * 7;
    return type;
}

static void apply(const struct lease *l)
{
    char a[16], g[16], d[16] = "-";
    ip_text(l->address, a);
    ip_text(l->router, g);
    if (l->dns_count)
        ip_text(l->dns[0], d);
    int rc = net_cfg(NET_CFG_SET_IPV4, l->address, l->netmask, l->router, 0);
    if (l->dns_count)
        net_cfg(NET_CFG_SET_DNS, 0, 0, 0, l->dns[0]);
    say("bound %s, gateway %s, dns %s, lease %u s%s", a, g, d, l->lease_s,
        rc < 0 ? " (configure failed)" : "");
    mkdir("/run/dhcp", 0755);
    char path[64];
    snprintf(path, sizeof(path), "/run/dhcp/%s.lease", ifname);
    FILE *f = fopen(path, "w");
    if (f) {
        char s[16];
        ip_text(l->server, s);
        fprintf(f, "address=%s\nrouter=%s\nserver=%s\nlease=%u\n", a, g, s, l->lease_s);
        for (uint32_t i = 0; i < l->dns_count; i++) {
            ip_text(l->dns[i], d);
            fprintf(f, "dns=%s\n", d);
        }
        fclose(f);
    }
}

static void restart(const char *why)
{
    if (why)
        say("%s, starting over", why);
    pc_getrandom(&xid, sizeof(xid));
    state = INIT;
    attempt = 0;
    next_send = now_ms();
}

static uint64_t backoff(void)
{
    unsigned s = 4u << (attempt < 4 ? attempt : 4);
    if (s > 64)
        s = 64;
    attempt++;
    return (uint64_t)s * 1000ULL;
}

static void on_term(int sig)
{
    (void)sig;
    stop = 1;
}

static void tick(void)
{
    uint64_t now = now_ms();
    struct net_if_info info;
    if (!iface(&info) || !info.link_up) {
        next_send = now + 1000;
        return;
    }
    if (state == BOUND || state == RENEWING || state == REBINDING) {
        uint64_t age = (now - bound_at) / 1000ULL;
        if (bound.lease_s != 0xFFFFFFFFU && age >= bound.lease_s) {
            net_cfg(NET_CFG_CLEAR_IPV4, 0, 0, 0, 0);
            restart("lease expired");
            return;
        }
        if (state == BOUND && age >= bound.t1_s) {
            state = RENEWING;
            attempt = 0;
            next_send = now;
            say("renewing");
        }
        if (state == RENEWING && age >= bound.t2_s) {
            state = REBINDING;
            attempt = 0;
            next_send = now;
            say("rebinding");
        }
        if (state == BOUND || now < next_send)
            return;
        bool ok = state == RENEWING
                      ? send_msg(REQUEST, bound.address, 0, 0, bound.server)
                      : send_msg(REQUEST, bound.address, 0, 0, 0xFFFFFFFFU);
        (void)ok;
        next_send = now + 10000;
        return;
    }
    if (now < next_send)
        return;
    if (state == INIT || state == SELECTING) {
        if (state == INIT)
            say("sending DISCOVER");
        state = SELECTING;
        send_msg(DISCOVER, 0, 0, 0, 0xFFFFFFFFU);
    } else if (state == REQUESTING) {
        if (attempt >= 4) {
            restart("no ACK");
            return;
        }
        send_msg(REQUEST, 0, offer.address, offer.server, 0xFFFFFFFFU);
    }
    next_send = now + backoff();
}

static void receive(void)
{
    uint8_t p[1500];
    struct sockaddr_in from;
    socklen_t fl = sizeof(from);
    ssize_t got = recvfrom(sock, p, sizeof(p), 0, (struct sockaddr *)&from, &fl);
    if (got <= 0)
        return;
    struct lease l;
    int type = parse(p, got, &l);
    if (!type)
        return;
    char a[16];
    if (state == SELECTING && type == OFFER && l.address && l.server) {
        offer = l;
        ip_text(l.address, a);
        say("OFFER %s, sending REQUEST", a);
        state = REQUESTING;
        attempt = 0;
        send_msg(REQUEST, 0, offer.address, offer.server, 0xFFFFFFFFU);
        next_send = now_ms() + backoff();
        return;
    }
    if ((state == REQUESTING || state == RENEWING || state == REBINDING) && type == ACK) {
        if (!l.server)
            l.server = state == REQUESTING ? offer.server : bound.server;
        bound = l;
        bound_at = now_ms();
        state = BOUND;
        attempt = 0;
        apply(&bound);
        return;
    }
    if ((state == REQUESTING || state == RENEWING || state == REBINDING) && type == NAK) {
        net_cfg(NET_CFG_CLEAR_IPV4, 0, 0, 0, 0);
        restart("NAK from server");
    }
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: dhcpc INTERFACE\n");
        return 1;
    }
    ifname = argv[1];
    setvbuf(stdout, 0, _IOLBF, 0);
    struct net_if_info info;
    if (!iface(&info)) {
        fprintf(stderr, "dhcpc: %s: no such interface\n", ifname);
        return 1;
    }
    memcpy(mac, info.mac, 6);
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    int one = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one));
    setsockopt(sock, SOL_SOCKET, SO_BINDTODEVICE, ifname, (socklen_t)strlen(ifname));
    struct sockaddr_in local;
    memset(&local, 0, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_port = htons(CLIENT_PORT);
    if (bind(sock, (struct sockaddr *)&local, sizeof(local)) < 0) {
        fprintf(stderr, "dhcpc: %s: cannot bind UDP 68: %s\n", ifname, strerror(errno));
        return 1;
    }
    struct timeval tv = {0, 500000};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    signal(SIGTERM, on_term);
    signal(SIGINT, on_term);
    say("started on UDP %d", CLIENT_PORT);
    restart(0);
    while (!stop) {
        tick();
        receive();
    }
    if (state == BOUND || state == RENEWING || state == REBINDING) {
        send_msg(RELEASE, bound.address, 0, bound.server, bound.server);
        net_cfg(NET_CFG_CLEAR_IPV4, 0, 0, 0, 0);
        say("released lease");
    }
    close(sock);
    return 0;
}
