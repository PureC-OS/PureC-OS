#include "../../libc/include/purec.h"
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#define NTP_PORT 123
#define NTP_PACKET 48
#define NTP_EPOCH_DELTA 2208988800ULL
#define NAME_MAX_COUNT 6
#define PEER_MAX 12
#define ADDRS_PER_NAME 2
#define REPLY_WINDOW_MS 2000
#define STEP_THRESHOLD_MS 128
#define PANIC_THRESHOLD_MS (1000LL * 1000)
#define RTC_SAVE_INTERVAL_S 660
#define RETRY_FIRST_S 8
#define RETRY_MAX_S 300
#define STATUS_DIR "/run/ntpd"
#define STATUS_PATH STATUS_DIR "/status"

struct peer {
    uint32_t address;
    char name[64];
    uint64_t sent_ntp;
    int64_t t1_ms;
    bool waiting;
    bool replied;
    bool denied;
    uint8_t stratum;
    double offset_ms;
    double delay_ms;
};

static char names[NAME_MAX_COUNT][64];
static int name_count;
static int minpoll = 6, maxpoll = 10;
static struct peer peers[PEER_MAX];
static int peer_count;
static int sock = -1;
static volatile sig_atomic_t stop;
static bool synced;
static int poll_exp;
static unsigned rounds_failed;
static time_t last_sync_unix, last_rtc_save;
static const struct peer *last_peer;
static double last_offset, last_delay;
static bool last_stepped;

static void on_term(int sig)
{
    (void)sig;
    stop = 1;
}

static int64_t wall_ms(void)
{
    struct time_info info;
    if (pc_time_get(&info) != 0)
        return 0;
    return info.unix_seconds * 1000 + (int64_t)info.millis;
}

static uint64_t mono_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000ULL + (uint64_t)t.tv_nsec / 1000000ULL;
}

static uint64_t ms_to_ntp(int64_t ms)
{
    uint64_t seconds = (uint64_t)(ms / 1000) + NTP_EPOCH_DELTA;
    uint64_t fraction = ((uint64_t)(ms % 1000) << 32) / 1000;
    return (seconds << 32) | fraction;
}

static double ntp_to_ms(uint64_t ntp)
{
    double seconds = (double)(ntp >> 32) - (double)NTP_EPOCH_DELTA;
    double fraction = (double)(ntp & 0xFFFFFFFFULL) / 4294967296.0;
    return (seconds + fraction) * 1000.0;
}

static uint64_t rd64(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 0; i < 8; i++)
        v = (v << 8) | p[i];
    return v;
}

static void wr64(uint8_t *p, uint64_t v)
{
    for (int i = 7; i >= 0; i--) {
        p[i] = (uint8_t)v;
        v >>= 8;
    }
}

static void add_name(const char *name)
{
    if (name_count >= NAME_MAX_COUNT || !name[0])
        return;
    for (int i = 0; i < name_count; i++)
        if (strcmp(names[i], name) == 0)
            return;
    snprintf(names[name_count++], sizeof(names[0]), "%s", name);
}

static void load_config(void)
{
    name_count = 0;
    FILE *f = fopen("/etc/ntp.conf", "r");
    if (f) {
        char line[160];
        while (fgets(line, sizeof(line), f)) {
            char word[32], value[96];
            if (line[0] == '#' || sscanf(line, "%31s %95s", word, value) != 2)
                continue;
            if (strcmp(word, "server") == 0 || strcmp(word, "pool") == 0)
                add_name(value);
            else if (strcmp(word, "minpoll") == 0)
                minpoll = atoi(value);
            else if (strcmp(word, "maxpoll") == 0)
                maxpoll = atoi(value);
        }
        fclose(f);
    }
    if (minpoll < 4)
        minpoll = 4;
    if (maxpoll > 17)
        maxpoll = 17;
    if (maxpoll < minpoll)
        maxpoll = minpoll;
    DIR *d = opendir("/run/dhcp");
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            if (!strstr(e->d_name, ".lease"))
                continue;
            char path[300], line[96];
            snprintf(path, sizeof(path), "/run/dhcp/%s", e->d_name);
            FILE *lf = fopen(path, "r");
            if (!lf)
                continue;
            while (fgets(line, sizeof(line), lf))
                if (strncmp(line, "ntp=", 4) == 0) {
                    line[strcspn(line, "\r\n")] = 0;
                    add_name(line + 4);
                }
            fclose(lf);
        }
        closedir(d);
    }
    if (!name_count) {
        add_name("pool.ntp.org");
        add_name("time.cloudflare.com");
    }
}

static bool peer_known(uint32_t address)
{
    for (int i = 0; i < peer_count; i++)
        if (peers[i].address == address)
            return true;
    return false;
}

static void resolve_peers(void)
{
    struct peer old[PEER_MAX];
    int old_count = peer_count;
    memcpy(old, peers, sizeof(old));
    peer_count = 0;
    for (int n = 0; n < name_count && peer_count < PEER_MAX; n++) {
        struct addrinfo hints, *res = 0;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        if (getaddrinfo(names[n], "123", &hints, &res) != 0 || !res) {
            syslog(LOG_WARNING, "cannot resolve %s", names[n]);
            continue;
        }
        int taken = 0;
        for (struct addrinfo *a = res; a && taken < ADDRS_PER_NAME && peer_count < PEER_MAX;
             a = a->ai_next) {
            uint32_t address = ntohl(((struct sockaddr_in *)a->ai_addr)->sin_addr.s_addr);
            if (!address || peer_known(address))
                continue;
            struct peer *p = &peers[peer_count++];
            memset(p, 0, sizeof(*p));
            p->address = address;
            snprintf(p->name, sizeof(p->name), "%s", names[n]);
            for (int o = 0; o < old_count; o++)
                if (old[o].address == address)
                    p->denied = old[o].denied;
            taken++;
        }
        freeaddrinfo(res);
    }
}

static void send_request(struct peer *p)
{
    uint8_t packet[NTP_PACKET];
    memset(packet, 0, sizeof(packet));
    packet[0] = (0 << 6) | (4 << 3) | 3;
    uint32_t salt = 0;
    pc_getrandom(&salt, sizeof(salt));
    p->t1_ms = wall_ms();
    p->sent_ntp = (ms_to_ntp(p->t1_ms) & ~0xFFFFULL) | (salt & 0xFFFF);
    wr64(packet + 40, p->sent_ntp);
    struct sockaddr_in to;
    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_port = htons(NTP_PORT);
    to.sin_addr.s_addr = htonl(p->address);
    p->waiting = sendto(sock, packet, sizeof(packet), 0, (struct sockaddr *)&to, sizeof(to)) ==
                 (ssize_t)sizeof(packet);
    p->replied = false;
}

static struct peer *peer_by_address(uint32_t address)
{
    for (int i = 0; i < peer_count; i++)
        if (peers[i].address == address)
            return &peers[i];
    return 0;
}

static void receive_reply(void)
{
    uint8_t packet[256];
    struct sockaddr_in from;
    socklen_t from_len = sizeof(from);
    ssize_t n = recvfrom(sock, packet, sizeof(packet), MSG_DONTWAIT, (struct sockaddr *)&from,
                         &from_len);
    int64_t t4 = wall_ms();
    if (n < NTP_PACKET || ntohs(from.sin_port) != NTP_PORT)
        return;
    struct peer *p = peer_by_address(ntohl(from.sin_addr.s_addr));
    if (!p || !p->waiting)
        return;
    uint8_t leap = packet[0] >> 6, version = (packet[0] >> 3) & 7, mode = packet[0] & 7;
    uint8_t stratum = packet[1];
    if (mode != 4 || version < 3 || rd64(packet + 24) != p->sent_ntp)
        return;
    p->waiting = false;
    if (stratum == 0) {
        char code[5] = {(char)packet[12], (char)packet[13], (char)packet[14], (char)packet[15], 0};
        syslog(LOG_WARNING, "%s (%s) kiss-o'-death %s", p->name,
               inet_ntoa(from.sin_addr), code);
        if (strcmp(code, "DENY") == 0 || strcmp(code, "RSTR") == 0)
            p->denied = true;
        return;
    }
    uint64_t t3_raw = rd64(packet + 40);
    if (leap == 3 || stratum > 15 || !t3_raw)
        return;
    double t1 = (double)p->t1_ms;
    double t2 = ntp_to_ms(rd64(packet + 32));
    double t3 = ntp_to_ms(t3_raw);
    double t4d = (double)t4;
    p->offset_ms = ((t2 - t1) + (t3 - t4d)) / 2.0;
    p->delay_ms = (t4d - t1) - (t3 - t2);
    if (p->delay_ms < 0)
        p->delay_ms = 0;
    p->stratum = stratum;
    p->replied = true;
}

static void collect(void)
{
    int active = 0;
    for (int i = 0; i < peer_count; i++)
        if (!peers[i].denied) {
            send_request(&peers[i]);
            active += peers[i].waiting;
        }
    if (!active)
        return;
    uint64_t deadline = mono_ms() + REPLY_WINDOW_MS;
    while (!stop) {
        uint64_t now = mono_ms();
        if (now >= deadline)
            break;
        bool pending = false;
        for (int i = 0; i < peer_count; i++)
            pending |= peers[i].waiting;
        if (!pending)
            break;
        struct pollfd pfd = {sock, POLLIN, 0};
        if (poll(&pfd, 1, (int)(deadline - now)) > 0 && (pfd.revents & POLLIN))
            receive_reply();
    }
}

static int by_delay(const void *a, const void *b)
{
    const struct peer *x = *(const struct peer *const *)a;
    const struct peer *y = *(const struct peer *const *)b;
    return x->delay_ms < y->delay_ms ? -1 : x->delay_ms > y->delay_ms;
}

static int by_offset(const void *a, const void *b)
{
    const struct peer *x = *(const struct peer *const *)a;
    const struct peer *y = *(const struct peer *const *)b;
    return x->offset_ms < y->offset_ms ? -1 : x->offset_ms > y->offset_ms;
}

static const struct peer *select_peer(void)
{
    const struct peer *list[PEER_MAX];
    int count = 0;
    for (int i = 0; i < peer_count; i++)
        if (peers[i].replied)
            list[count++] = &peers[i];
    if (!count)
        return 0;
    qsort(list, (size_t)count, sizeof(list[0]), by_delay);
    if (count > 3)
        count = 3;
    qsort(list, (size_t)count, sizeof(list[0]), by_offset);
    return list[count / 2];
}

static void write_status(void)
{
    mkdir(STATUS_DIR, 0755);
    FILE *f = fopen(STATUS_PATH ".tmp", "w");
    if (!f)
        return;
    fprintf(f, "synced=%s\n", synced ? "yes" : "no");
    if (last_peer) {
        struct in_addr a = {htonl(last_peer->address)};
        fprintf(f, "server=%s\nname=%s\nstratum=%u\n", inet_ntoa(a), last_peer->name,
                last_peer->stratum);
        fprintf(f, "offset_ms=%.3f\ndelay_ms=%.3f\nmode=%s\n", last_offset, last_delay,
                last_stepped ? "step" : "slew");
    }
    fprintf(f, "last_sync=%lld\npoll=%d\n", (long long)last_sync_unix, 1 << poll_exp);
    for (int i = 0; i < peer_count; i++) {
        struct in_addr a = {htonl(peers[i].address)};
        fprintf(f, "peer=%s %s %s", inet_ntoa(a), peers[i].name,
                peers[i].denied ? "denied" : peers[i].replied ? "ok" : "no-reply");
        if (peers[i].replied)
            fprintf(f, " stratum=%u offset=%.3f delay=%.3f", peers[i].stratum,
                    peers[i].offset_ms, peers[i].delay_ms);
        fputc('\n', f);
    }
    fclose(f);
    rename(STATUS_PATH ".tmp", STATUS_PATH);
}

static void discipline(const struct peer *p)
{
    int64_t offset = (int64_t)(p->offset_ms < 0 ? p->offset_ms - 0.5 : p->offset_ms + 0.5);
    int64_t magnitude = offset < 0 ? -offset : offset;
    struct in_addr a = {htonl(p->address)};
    if (synced && magnitude > PANIC_THRESHOLD_MS) {
        syslog(LOG_ERR, "%s (%s) offset %lld ms exceeds panic threshold, ignored", p->name,
               inet_ntoa(a), (long long)offset);
        return;
    }
    int rc;
    last_stepped = !synced || magnitude >= STEP_THRESHOLD_MS;
    if (last_stepped)
        rc = pc_clock_adjust(CLOCK_ADJ_STEP, offset, 0);
    else
        rc = pc_clock_adjust(CLOCK_ADJ_SLEW, offset, 0);
    if (rc < 0) {
        syslog(LOG_ERR, "cannot adjust clock: %s", strerror(-rc));
        return;
    }
    bool first = !synced;
    synced = true;
    last_peer = p;
    last_offset = p->offset_ms;
    last_delay = p->delay_ms;
    last_sync_unix = time(0);
    if (first || last_stepped)
        syslog(LOG_NOTICE, "%s to %s (%s) stratum %u: offset %+lld ms, delay %.1f ms",
               last_stepped ? "clock stepped" : "synchronized", p->name, inet_ntoa(a),
               p->stratum, (long long)offset, p->delay_ms);
    if (last_stepped) {
        poll_exp = minpoll;
        last_rtc_save = last_sync_unix;
        return;
    }
    if (magnitude < 50 && poll_exp < maxpoll)
        poll_exp++;
    else if (magnitude >= 100 && poll_exp > minpoll)
        poll_exp--;
    if (last_sync_unix - last_rtc_save >= RTC_SAVE_INTERVAL_S) {
        pc_clock_adjust(CLOCK_ADJ_SAVE_RTC, 0, 0);
        last_rtc_save = last_sync_unix;
    }
}

static int open_socket(void)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return -1;
    struct sockaddr_in local;
    memset(&local, 0, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_port = htons(NTP_PORT);
    if (bind(fd, (struct sockaddr *)&local, sizeof(local)) < 0) {
        syslog(LOG_WARNING, "cannot bind UDP %d (%s), using an ephemeral port", NTP_PORT,
               strerror(errno));
        local.sin_port = 0;
        if (bind(fd, (struct sockaddr *)&local, sizeof(local)) < 0) {
            close(fd);
            return -1;
        }
    }
    return fd;
}

static void nap(unsigned seconds)
{
    for (unsigned i = 0; i < seconds && !stop; i++)
        sleep(1);
}

int main(int argc, char **argv)
{
    bool once = argc > 1 && strcmp(argv[1], "-q") == 0;
    signal(SIGTERM, on_term);
    signal(SIGINT, on_term);
    load_config();
    poll_exp = minpoll;
    sock = open_socket();
    if (sock < 0) {
        syslog(LOG_ERR, "cannot open UDP socket");
        return 1;
    }
    syslog(LOG_INFO, "started (pid %d), %d server name(s), poll %d..%d s", (int)getpid(),
           name_count, 1 << minpoll, 1 << maxpoll);
    while (!stop) {
        load_config();
        resolve_peers();
        if (peer_count)
            collect();
        const struct peer *best = select_peer();
        unsigned wait;
        if (best) {
            rounds_failed = 0;
            discipline(best);
            wait = 1u << poll_exp;
        } else {
            rounds_failed++;
            if (rounds_failed == 1 || rounds_failed % 10 == 0)
                syslog(LOG_WARNING, "no usable reply from %d server(s)", peer_count);
            wait = RETRY_FIRST_S << (rounds_failed < 6 ? rounds_failed - 1 : 5);
            if (wait > RETRY_MAX_S)
                wait = RETRY_MAX_S;
        }
        write_status();
        if (once)
            return best ? 0 : 1;
        nap(wait);
    }
    if (synced)
        pc_clock_adjust(CLOCK_ADJ_SAVE_RTC, 0, 0);
    syslog(LOG_INFO, "stopped");
    return 0;
}
