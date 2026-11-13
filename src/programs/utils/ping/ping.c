#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include "../lib/common.h"
#include <signal.h>
#include "libc/include/purec.h"

static volatile int interrupted;

static void on_int(int sig)
{
    (void)sig;
    interrupted = 1;
}

static const char *ping_error(int32_t status)
{
    switch (status) {
    case -2: return "no active network interface";
    case -3: return "network is not configured; waiting for DHCP";
    case -4: return "cannot resolve host name";
    case -5: return "request timeout";
    case -6: return "another network query is active";
    case -7: return "network transmission failed";
    }
    return "invalid target or arguments";
}

int main(int argc, char **argv)
{
    util_init(argc, argv);
    struct opts o;
    opts_begin(&o, argc, argv);
    long count = 4, timeout = 2000;
    double interval = 1.0;
    bool quiet = false;
    int c;
    while ((c = opts_next(&o, "c:i:W:qt")) != -1) {
        switch (c) {
        case 'c': if (!util_parse_long(o.arg, &count) || count < 0) return util_err("invalid count '%s'", o.arg); break;
        case 'i': interval = atof(o.arg); if (interval < 0.2) interval = 0.2; break;
        case 'W': timeout = (long)(atof(o.arg) * 1000); break;
        case 'q': quiet = true; break;
        case 't': count = 0; break;
        case 'L': if (opts_long(&o, "help")) { util_usage("[-q] [-c COUNT] [-i INTERVAL] [-W TIMEOUT] HOST"); return 0; }
            return util_bad_option(&o, c);
        default: return util_bad_option(&o, c);
        }
    }
    if (o.count != 1) return util_err(o.count ? "extra operand '%s'" : "missing host operand", o.count ? o.operands[1] : ""), 2;
    const char *target = o.operands[0];
    if (strlen(target) >= NETWORK_PING_TARGET_CAPACITY) return util_err("target is too long"), 2;
    char resolved_ip[16];
    struct addrinfo hints, *found = 0;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    int gai = getaddrinfo(target, 0, &hints, &found);
    if (gai != 0 || !found)
        return util_err("cannot resolve %s: %s", target, gai_strerror(gai)), 2;
    snprintf(resolved_ip, sizeof(resolved_ip), "%s",
             inet_ntoa(((struct sockaddr_in *)found->ai_addr)->sin_addr));
    freeaddrinfo(found);
    signal(SIGINT, on_int);
    bool color = util_color(1, NULL);
    bool heading = false;
    long sent = 0, received = 0;
    unsigned min_ms = 0xffffffffu, max_ms = 0;
    unsigned long long sum = 0;
    for (long seq = 1; !interrupted && (count == 0 || seq <= count); seq++) {
        struct network_ping_result r;
        memset(&r, 0, sizeof(r));
        int32_t st = pc_ping(resolved_ip, (uint16_t)seq, (uint32_t)timeout, &r);
        sent++;
        if (st == 0) {
            if (!heading) {
                printf("PING %s (%u.%u.%u.%u) 56 data bytes\n", target, (r.address >> 24) & 255, (r.address >> 16) & 255,
                       (r.address >> 8) & 255, r.address & 255);
                heading = true;
            }
            received++;
            sum += r.round_trip_ms;
            if (r.round_trip_ms < min_ms) min_ms = r.round_trip_ms;
            if (r.round_trip_ms > max_ms) max_ms = r.round_trip_ms;
            if (!quiet) {
                const char *tc = !color ? "" : r.round_trip_ms < 50 ? C_GREEN : r.round_trip_ms < 200 ? C_YELLOW : C_RED;
                printf("64 bytes from %u.%u.%u.%u: icmp_seq=%u ttl=%u time=%s%u ms%s\n", (r.address >> 24) & 255,
                       (r.address >> 16) & 255, (r.address >> 8) & 255, r.address & 255, r.sequence, r.ttl, tc,
                       r.round_trip_ms, color ? C_RESET : "");
            }
        } else {
            if (st != -5) {
                util_err("%s", ping_error(st));
                return 2;
            }
            if (!quiet) printf("%sRequest timeout for icmp_seq %ld%s\n", color ? C_RED : "", seq, color ? C_RESET : "");
        }
        fflush(stdout);
        if (count == 0 || seq < count)
            for (long waited = 0; waited < (long)(interval * 1000) && !interrupted; waited += 100) usleep(100000);
    }
    printf("\n--- %s ping statistics ---\n", target);
    printf("%ld packets transmitted, %ld received, %s%ld%% packet loss%s\n", sent, received,
           color ? (received == sent ? C_GREEN : C_RED) : "", sent ? (sent - received) * 100 / sent : 0, color ? C_RESET : "");
    if (received)
        printf("rtt min/avg/max = %u/%llu/%u ms\n", min_ms, sum / (unsigned long long)received, max_ms);
    return received ? 0 : 1;
}
