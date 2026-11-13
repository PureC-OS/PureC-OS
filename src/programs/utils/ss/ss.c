#include "../lib/common.h"
#include "libc/include/purec.h"
#include <arpa/inet.h>
#include <netdb.h>

#define SHOW_LISTEN 1
#define SHOW_CONNECTED 2

static const char *const tcp_states[] = {
    "CLOSED",   "LISTEN",     "SYN-SENT", "SYN-RECV", "ESTAB",    "FIN-WAIT-1",
    "FIN-WAIT-2", "CLOSE-WAIT", "CLOSING", "LAST-ACK", "TIME-WAIT",
};

static bool numeric, show_process, show_extended, no_header, color;
static unsigned show_mask = SHOW_CONNECTED;
static bool want_tcp, want_udp;

static const char *paint(const char *code) { return color ? code : ""; }

static const char *state_name(const struct net_socket_entry *e)
{
    if (e->state == NET_SOCK_STATE_UNCONN)
        return "UNCONN";
    if (e->state < sizeof(tcp_states) / sizeof(tcp_states[0]))
        return tcp_states[e->state];
    return "UNKNOWN";
}

static bool is_listening(const struct net_socket_entry *e)
{
    return e->state == NET_SOCK_STATE_UNCONN ||
           (e->protocol == NET_SOCK_TCP && e->state == 1);
}

static void endpoint(char *out, size_t cap, uint32_t address, uint16_t port,
                     const char *device, const char *proto, bool peer)
{
    char host[24], serv[32];
    if (peer && !address && !port) {
        snprintf(out, cap, "*:*");
        return;
    }
    if (address)
        snprintf(host, sizeof(host), "%u.%u.%u.%u", (address >> 24) & 255,
                 (address >> 16) & 255, (address >> 8) & 255, address & 255);
    else
        snprintf(host, sizeof(host), "%s", peer ? "*" : "0.0.0.0");
    if (!port) {
        snprintf(serv, sizeof(serv), "*");
    } else {
        struct servent *s = numeric ? 0 : getservbyport(htons(port), proto);
        if (s && s->s_name)
            snprintf(serv, sizeof(serv), "%s", s->s_name);
        else
            snprintf(serv, sizeof(serv), "%u", port);
    }
    if (device && device[0] && !peer)
        snprintf(out, cap, "%s%%%s:%s", host, device, serv);
    else
        snprintf(out, cap, "%s:%s", host, serv);
}

static bool wanted(const struct net_socket_entry *e)
{
    if (e->protocol == NET_SOCK_TCP && !want_tcp)
        return false;
    if (e->protocol == NET_SOCK_UDP && !want_udp)
        return false;
    return (show_mask & (is_listening(e) ? SHOW_LISTEN : SHOW_CONNECTED)) != 0;
}

static int load(struct net_socket_entry **out)
{
    int cap = NET_SOCKETS_MAX, total = 0;
    struct net_socket_entry *list = malloc(sizeof(*list) * (size_t)cap);
    if (!list)
        return -1;
    for (;;) {
        int n = pc_net_sockets(list + total, NET_SOCKETS_MAX, (uint32_t)total);
        if (n < 0) {
            free(list);
            return -1;
        }
        total += n;
        if (n < NET_SOCKETS_MAX)
            break;
        struct net_socket_entry *grown =
            realloc(list, sizeof(*list) * (size_t)(total + NET_SOCKETS_MAX));
        if (!grown)
            break;
        list = grown;
    }
    *out = list;
    return total;
}

static void print_process(const struct net_socket_entry *e)
{
    if (e->flags & NET_SOCK_FLAG_KERNEL) {
        printf(" %skernel%s", paint(C_GRAY), paint(C_RESET));
        return;
    }
    if (e->flags & NET_SOCK_FLAG_ORPHAN) {
        printf(" %sorphan%s", paint(C_GRAY), paint(C_RESET));
        return;
    }
    if (!e->pid)
        return;
    printf(" users:((\"%s%s%s\",pid=%u,fd=%u))", paint(C_CYAN), e->process, paint(C_RESET),
           e->pid, e->fd);
}

static void print_extended(const struct net_socket_entry *e)
{
    if (e->pid && !(e->flags & NET_SOCK_FLAG_KERNEL)) {
        char user[32];
        printf(" uid:%u(%s)", e->uid, util_user(e->uid, user, sizeof(user)));
    }
    if (e->flags & NET_SOCK_FLAG_REUSE)
        printf(" reuseaddr");
    if (e->flags & NET_SOCK_FLAG_BROADCAST)
        printf(" broadcast");
}

static void summary(const struct net_socket_entry *list, int n)
{
    unsigned tcp = 0, udp = 0, estab = 0, listen = 0, timewait = 0, orphan = 0, kernel = 0;
    for (int i = 0; i < n; i++) {
        const struct net_socket_entry *e = &list[i];
        if (e->flags & NET_SOCK_FLAG_KERNEL)
            kernel++;
        if (e->protocol == NET_SOCK_UDP) {
            udp++;
            continue;
        }
        tcp++;
        if (e->state == 4)
            estab++;
        else if (e->state == 1)
            listen++;
        else if (e->state == 10)
            timewait++;
        if (e->flags & NET_SOCK_FLAG_ORPHAN)
            orphan++;
    }
    printf("Total: %d\n", n);
    printf("TCP:   %u (estab %u, listen %u, orphaned %u, timewait %u)\n", tcp, estab, listen,
           orphan, timewait);
    printf("UDP:   %u\n", udp);
    printf("Kernel-owned: %u\n", kernel);
}

static void usage(void)
{
    printf("Usage: ss [OPTIONS]\n"
           "  -t, --tcp         TCP sockets\n"
           "  -u, --udp         UDP sockets\n"
           "  -l, --listening   only listening (TCP LISTEN, UDP UNCONN)\n"
           "  -a, --all         listening and connected\n"
           "  -n, --numeric     do not resolve service names\n"
           "  -p, --processes   show the process using the socket\n"
           "  -e, --extended    show uid and socket options\n"
           "  -s, --summary     print counters\n"
           "  -H, --no-header   suppress the header line\n"
           "  -4                IPv4 only (the only family)\n");
}

int main(int argc, char **argv)
{
    util_init(argc, argv);
    color = util_color(1, "auto");
    struct opts o;
    opts_begin(&o, argc, argv);
    bool summary_only = false;
    int c;
    while ((c = opts_next(&o, "tulanpesH4h")) != -1) {
        switch (c) {
        case 't': want_tcp = true; break;
        case 'u': want_udp = true; break;
        case 'l': show_mask = SHOW_LISTEN; break;
        case 'a': show_mask = SHOW_LISTEN | SHOW_CONNECTED; break;
        case 'n': numeric = true; break;
        case 'p': show_process = true; break;
        case 'e': show_extended = true; break;
        case 's': summary_only = true; break;
        case 'H': no_header = true; break;
        case '4': break;
        case 'h': usage(); return 0;
        case 'L':
            if (opts_long(&o, "help")) { usage(); return 0; }
            if (opts_long(&o, "tcp")) { want_tcp = true; break; }
            if (opts_long(&o, "udp")) { want_udp = true; break; }
            if (opts_long(&o, "listening")) { show_mask = SHOW_LISTEN; break; }
            if (opts_long(&o, "all")) { show_mask = SHOW_LISTEN | SHOW_CONNECTED; break; }
            if (opts_long(&o, "numeric")) { numeric = true; break; }
            if (opts_long(&o, "processes")) { show_process = true; break; }
            if (opts_long(&o, "extended")) { show_extended = true; break; }
            if (opts_long(&o, "summary")) { summary_only = true; break; }
            if (opts_long(&o, "no-header")) { no_header = true; break; }
            return util_bad_option(&o, c);
        default: return util_bad_option(&o, c);
        }
    }
    if (o.count)
        return util_err("filters are not supported: '%s'", o.operands[0]);
    if (!want_tcp && !want_udp)
        want_tcp = want_udp = true;
    struct net_socket_entry *list = 0;
    int n = load(&list);
    if (n < 0)
        return util_err("cannot read the socket table");
    if (summary_only) {
        summary(list, n);
        free(list);
        return 0;
    }
    if (!no_header)
        printf("%s%-5s %-10s %6s %6s %-27s %-21s%s%s\n", paint(C_BOLD), "Netid", "State",
               "Recv-Q", "Send-Q", "Local Address:Port", "Peer Address:Port",
               show_process ? " Process" : "", paint(C_RESET));
    for (int i = 0; i < n; i++) {
        const struct net_socket_entry *e = &list[i];
        if (!wanted(e))
            continue;
        const char *proto = e->protocol == NET_SOCK_TCP ? "tcp" : "udp";
        char local[48], peer[48];
        endpoint(local, sizeof(local), e->local_address, e->local_port, e->device, proto, false);
        endpoint(peer, sizeof(peer), e->remote_address, e->remote_port, 0, proto, true);
        const char *state = state_name(e);
        const char *tint = is_listening(e) ? C_GREEN : e->state == 4 ? C_CYAN : C_YELLOW;
        printf("%-5s %s%-10s%s %6u %6u %-27s %-21s", proto, paint(tint), state, paint(C_RESET),
               e->recv_queue, e->send_queue, local, peer);
        if (show_process)
            print_process(e);
        if (show_extended)
            print_extended(e);
        putchar('\n');
    }
    free(list);
    return 0;
}
