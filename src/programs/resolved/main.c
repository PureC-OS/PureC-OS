#include "../../libc/include/purec.h"
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <syslog.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define LISTEN_PORT 53
#define MAX_PACKET 512
#define PENDING_MAX 64
#define CACHE_MAX 128
#define UPSTREAM_MAX 4
#define QUERY_TIMEOUT_MS 2000
#define TTL_CAP 3600
#define TTL_FLOOR 5
#define HOSTS_TTL 60

struct pending {
    bool used;
    uint16_t client_id;
    uint16_t upstream_id;
    struct sockaddr_in client;
    uint8_t query[MAX_PACKET];
    uint16_t query_len;
    uint64_t sent_at;
    uint32_t upstream_index;
    uint32_t tries;
};

struct cache_entry {
    bool used;
    char key[260];
    uint8_t answer[MAX_PACKET];
    uint16_t answer_len;
    uint64_t expires_at;
    uint64_t stored_at;
};

static int listen_fd = -1, upstream_fd = -1;
static struct pending pending[PENDING_MAX];
static struct cache_entry cache[CACHE_MAX];
static uint32_t upstream[UPSTREAM_MAX];
static uint32_t upstream_count;
static uint16_t next_id = 1;
static volatile sig_atomic_t stop;
static unsigned long served, hits, forwarded;

static uint64_t now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000ULL + (uint64_t)t.tv_nsec / 1000000ULL;
}

static void on_term(int sig)
{
    (void)sig;
    stop = 1;
}

static void load_upstreams(void)
{
    upstream_count = 0;
    FILE *f = fopen("/etc/resolved.conf", "r");
    if (f) {
        char line[128];
        while (fgets(line, sizeof(line), f) && upstream_count < UPSTREAM_MAX) {
            char word[32], addr[64];
            if (sscanf(line, "%31s %63s", word, addr) == 2 && strcmp(word, "server") == 0) {
                struct in_addr a;
                if (inet_aton(addr, &a) && (ntohl(a.s_addr) >> 24) != 127)
                    upstream[upstream_count++] = ntohl(a.s_addr);
            }
        }
        fclose(f);
    }
    if (upstream_count)
        return;
    struct net_if_info list[NET_IF_MAX_COUNT];
    int32_t n = pc_net_if_list(list, NET_IF_MAX_COUNT);
    for (int32_t i = 0; i < n && upstream_count < UPSTREAM_MAX; i++) {
        uint32_t d = list[i].dns_server;
        if (!d || (d >> 24) == 127)
            continue;
        bool dup = false;
        for (uint32_t k = 0; k < upstream_count; k++)
            dup |= upstream[k] == d;
        if (!dup)
            upstream[upstream_count++] = d;
    }
}

static int skip_name(const uint8_t *p, int len, int at)
{
    while (at < len) {
        uint8_t l = p[at];
        if (l == 0)
            return at + 1;
        if ((l & 0xC0) == 0xC0)
            return at + 2;
        at += l + 1;
    }
    return -1;
}

static bool question_key(const uint8_t *p, int len, char *key, size_t cap, int *end)
{
    if (len < 12 || (p[4] << 8 | p[5]) != 1)
        return false;
    int at = 12;
    size_t k = 0;
    while (at < len && p[at]) {
        uint8_t l = p[at++];
        if ((l & 0xC0) || at + l > len || k + l + 2 >= cap)
            return false;
        for (uint8_t i = 0; i < l; i++) {
            char c = (char)p[at + i];
            key[k++] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
        }
        key[k++] = '.';
        at += l;
    }
    if (at + 5 > len)
        return false;
    at++;
    snprintf(key + k, cap - k, "/%u/%u", p[at] << 8 | p[at + 1], p[at + 2] << 8 | p[at + 3]);
    *end = at + 4;
    return true;
}

static uint32_t min_ttl(const uint8_t *p, int len)
{
    int qd = p[4] << 8 | p[5];
    int an = p[6] << 8 | p[7];
    int at = 12;
    for (int i = 0; i < qd; i++) {
        at = skip_name(p, len, at);
        if (at < 0)
            return 0;
        at += 4;
    }
    uint32_t best = TTL_CAP;
    for (int i = 0; i < an && at < len; i++) {
        at = skip_name(p, len, at);
        if (at < 0 || at + 10 > len)
            break;
        uint32_t ttl = (uint32_t)p[at + 4] << 24 | (uint32_t)p[at + 5] << 16 |
                       (uint32_t)p[at + 6] << 8 | p[at + 7];
        if (ttl < best)
            best = ttl;
        at += 10 + (p[at + 8] << 8 | p[at + 9]);
    }
    if ((p[3] & 15) == 3)
        best = 60;
    return best < TTL_FLOOR ? TTL_FLOOR : best;
}

static void reply(const struct sockaddr_in *to, const uint8_t *p, int len)
{
    sendto(listen_fd, p, (size_t)len, 0, (const struct sockaddr *)to, sizeof(*to));
    served++;
}

static bool answer_from_hosts(const uint8_t *q, int qlen, const char *key, int qend,
                              const struct sockaddr_in *client)
{
    const char *slash = strchr(key, '/');
    if (!slash || strcmp(slash, "/1/1") != 0)
        return false;
    char name[256];
    size_t n = (size_t)(slash - key);
    if (n == 0 || n >= sizeof(name))
        return false;
    memcpy(name, key, n - 1);
    name[n - 1] = 0;
    FILE *f = fopen("/etc/hosts", "r");
    if (!f)
        return false;
    char line[256];
    uint32_t found = 0;
    while (!found && fgets(line, sizeof(line), f)) {
        char *hash = strchr(line, '#');
        if (hash)
            *hash = 0;
        char *tok = strtok(line, " \t\r\n");
        if (!tok)
            continue;
        struct in_addr a;
        if (!inet_aton(tok, &a))
            continue;
        while ((tok = strtok(0, " \t\r\n")))
            if (strcasecmp(tok, name) == 0)
                found = ntohl(a.s_addr);
    }
    fclose(f);
    if (!found)
        return false;
    uint8_t r[MAX_PACKET];
    if (qend + 16 > (int)sizeof(r))
        return false;
    memcpy(r, q, (size_t)qend);
    r[2] = 0x81 | (q[2] & 0x01);
    r[3] = 0x80;
    r[6] = 0;
    r[7] = 1;
    r[8] = r[9] = r[10] = r[11] = 0;
    int at = qend;
    r[at++] = 0xC0;
    r[at++] = 12;
    r[at++] = 0;
    r[at++] = 1;
    r[at++] = 0;
    r[at++] = 1;
    r[at++] = 0;
    r[at++] = 0;
    r[at++] = 0;
    r[at++] = HOSTS_TTL;
    r[at++] = 0;
    r[at++] = 4;
    r[at++] = (uint8_t)(found >> 24);
    r[at++] = (uint8_t)(found >> 16);
    r[at++] = (uint8_t)(found >> 8);
    r[at++] = (uint8_t)found;
    (void)qlen;
    reply(client, r, at);
    return true;
}

static struct cache_entry *cache_find(const char *key)
{
    uint64_t now = now_ms();
    for (int i = 0; i < CACHE_MAX; i++)
        if (cache[i].used && strcmp(cache[i].key, key) == 0) {
            if (now >= cache[i].expires_at) {
                cache[i].used = false;
                return 0;
            }
            return &cache[i];
        }
    return 0;
}

static void cache_store(const char *key, const uint8_t *p, int len)
{
    if ((p[3] & 15) != 0 && (p[3] & 15) != 3)
        return;
    if (p[2] & 0x02)
        return;
    int slot = 0;
    uint64_t oldest = UINT64_MAX;
    for (int i = 0; i < CACHE_MAX; i++) {
        if (!cache[i].used || strcmp(cache[i].key, key) == 0) {
            slot = i;
            break;
        }
        if (cache[i].expires_at < oldest) {
            oldest = cache[i].expires_at;
            slot = i;
        }
    }
    struct cache_entry *e = &cache[slot];
    e->used = true;
    snprintf(e->key, sizeof(e->key), "%s", key);
    memcpy(e->answer, p, (size_t)len);
    e->answer_len = (uint16_t)len;
    e->stored_at = now_ms();
    e->expires_at = e->stored_at + (uint64_t)min_ttl(p, len) * 1000ULL;
}

static void age_ttls(uint8_t *p, int len, uint32_t elapsed)
{
    int qd = p[4] << 8 | p[5];
    int records = (p[6] << 8 | p[7]) + (p[8] << 8 | p[9]) + (p[10] << 8 | p[11]);
    int at = 12;
    for (int i = 0; i < qd; i++) {
        at = skip_name(p, len, at);
        if (at < 0)
            return;
        at += 4;
    }
    for (int i = 0; i < records && at < len; i++) {
        at = skip_name(p, len, at);
        if (at < 0 || at + 10 > len)
            return;
        if ((p[at] << 8 | p[at + 1]) != 41) {
            uint32_t ttl = (uint32_t)p[at + 4] << 24 | (uint32_t)p[at + 5] << 16 |
                           (uint32_t)p[at + 6] << 8 | p[at + 7];
            ttl = ttl > elapsed ? ttl - elapsed : 0;
            p[at + 4] = (uint8_t)(ttl >> 24);
            p[at + 5] = (uint8_t)(ttl >> 16);
            p[at + 6] = (uint8_t)(ttl >> 8);
            p[at + 7] = (uint8_t)ttl;
        }
        at += 10 + (p[at + 8] << 8 | p[at + 9]);
    }
}

static void send_upstream(struct pending *q)
{
    if (!upstream_count)
        return;
    struct sockaddr_in to;
    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_port = htons(53);
    to.sin_addr.s_addr = htonl(upstream[q->upstream_index % upstream_count]);
    q->query[0] = (uint8_t)(q->upstream_id >> 8);
    q->query[1] = (uint8_t)q->upstream_id;
    q->sent_at = now_ms();
    q->tries++;
    sendto(upstream_fd, q->query, q->query_len, 0, (struct sockaddr *)&to, sizeof(to));
    forwarded++;
}

static void servfail(const struct pending *q)
{
    uint8_t r[MAX_PACKET];
    memcpy(r, q->query, q->query_len);
    r[0] = (uint8_t)(q->client_id >> 8);
    r[1] = (uint8_t)q->client_id;
    r[2] = 0x81 | (q->query[2] & 0x01);
    r[3] = 0x82;
    r[6] = r[7] = r[8] = r[9] = r[10] = r[11] = 0;
    reply(&q->client, r, q->query_len);
}

static void on_client(void)
{
    uint8_t p[MAX_PACKET];
    struct sockaddr_in from;
    socklen_t fl = sizeof(from);
    ssize_t got = recvfrom(listen_fd, p, sizeof(p), 0, (struct sockaddr *)&from, &fl);
    if (got < 12 || (p[2] & 0x80))
        return;
    char key[260];
    int qend = 0;
    if (!question_key(p, (int)got, key, sizeof(key), &qend))
        return;
    if (answer_from_hosts(p, (int)got, key, qend, &from))
        return;
    struct cache_entry *c = cache_find(key);
    if (c) {
        uint8_t r[MAX_PACKET];
        memcpy(r, c->answer, c->answer_len);
        r[0] = p[0];
        r[1] = p[1];
        age_ttls(r, c->answer_len, (uint32_t)((now_ms() - c->stored_at) / 1000ULL));
        reply(&from, r, c->answer_len);
        hits++;
        return;
    }
    load_upstreams();
    struct pending *q = 0;
    for (int i = 0; i < PENDING_MAX && !q; i++)
        if (!pending[i].used)
            q = &pending[i];
    if (!q || !upstream_count) {
        struct pending tmp;
        memset(&tmp, 0, sizeof(tmp));
        memcpy(tmp.query, p, (size_t)got);
        tmp.query_len = (uint16_t)got;
        tmp.client = from;
        tmp.client_id = (uint16_t)(p[0] << 8 | p[1]);
        servfail(&tmp);
        return;
    }
    memset(q, 0, sizeof(*q));
    q->used = true;
    q->client = from;
    q->client_id = (uint16_t)(p[0] << 8 | p[1]);
    q->upstream_id = next_id++;
    if (!next_id)
        next_id = 1;
    memcpy(q->query, p, (size_t)got);
    q->query_len = (uint16_t)got;
    send_upstream(q);
}

static void on_upstream(void)
{
    uint8_t p[MAX_PACKET];
    struct sockaddr_in from;
    socklen_t fl = sizeof(from);
    ssize_t got = recvfrom(upstream_fd, p, sizeof(p), 0, (struct sockaddr *)&from, &fl);
    if (got < 12 || ntohs(from.sin_port) != 53)
        return;
    uint16_t id = (uint16_t)(p[0] << 8 | p[1]);
    for (int i = 0; i < PENDING_MAX; i++) {
        struct pending *q = &pending[i];
        if (!q->used || q->upstream_id != id)
            continue;
        char key[260];
        int qend;
        if (question_key(p, (int)got, key, sizeof(key), &qend))
            cache_store(key, p, (int)got);
        p[0] = (uint8_t)(q->client_id >> 8);
        p[1] = (uint8_t)q->client_id;
        reply(&q->client, p, (int)got);
        q->used = false;
        return;
    }
}

static void expire(void)
{
    uint64_t now = now_ms();
    for (int i = 0; i < PENDING_MAX; i++) {
        struct pending *q = &pending[i];
        if (!q->used || now - q->sent_at < QUERY_TIMEOUT_MS)
            continue;
        if (q->tries < 2 * (upstream_count ? upstream_count : 1)) {
            q->upstream_index++;
            send_upstream(q);
        } else {
            servfail(q);
            q->used = false;
        }
    }
}

int main(void)
{
    setvbuf(stdout, 0, _IOLBF, 0);
    listen_fd = socket(AF_INET, SOCK_DGRAM, 0);
    upstream_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in local;
    memset(&local, 0, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_port = htons(LISTEN_PORT);
    local.sin_addr.s_addr = htonl(0x7F000001U);
    if (listen_fd < 0 || upstream_fd < 0 ||
        bind(listen_fd, (struct sockaddr *)&local, sizeof(local)) < 0) {
        syslog(LOG_INFO, "cannot bind 127.0.0.1:53: %s\n", strerror(errno));
        return 1;
    }
    signal(SIGTERM, on_term);
    signal(SIGINT, on_term);
    load_upstreams();
    syslog(LOG_INFO, "listening on 127.0.0.1:53, %u upstream server(s)\n", upstream_count);
    while (!stop) {
        struct pollfd fds[2] = {{.fd = listen_fd, .events = POLLIN}, {.fd = upstream_fd, .events = POLLIN}};
        int ready = poll(fds, 2, 250);
        if (ready > 0) {
            if (fds[0].revents & POLLIN)
                on_client();
            if (fds[1].revents & POLLIN)
                on_upstream();
        }
        expire();
    }
    syslog(LOG_INFO, "stopped (served %lu, cache hits %lu, forwarded %lu)\n", served, hits,
           forwarded);
    return 0;
}
