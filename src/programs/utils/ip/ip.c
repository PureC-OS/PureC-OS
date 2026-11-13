#include "../lib/common.h"
#include "libc/include/purec.h"

#define C_RESET "\x1b[0m"
#define C_BOLD "\x1b[1m"
#define C_GREEN "\x1b[32m"
#define C_RED "\x1b[31m"
#define C_CYAN "\x1b[36m"

static bool color;

static const char *paint(const char *code) { return color ? code : ""; }

static void addr_text(uint32_t a, char *out, size_t cap)
{
    snprintf(out, cap, "%u.%u.%u.%u", (a >> 24) & 255, (a >> 16) & 255, (a >> 8) & 255, a & 255);
}

static bool parse_addr(const char *text, uint32_t *out)
{
    unsigned p[4];
    char tail;
    if (sscanf(text, "%u.%u.%u.%u%c", &p[0], &p[1], &p[2], &p[3], &tail) != 4)
        return false;
    for (int i = 0; i < 4; i++)
        if (p[i] > 255)
            return false;
    *out = (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
    return true;
}

static int prefix_of(uint32_t mask)
{
    int n = 0;
    while (mask & 0x80000000U) {
        n++;
        mask <<= 1;
    }
    return n;
}

static bool dynamic_lease(const struct net_if_info *f)
{
    if (f->dhcp_bound)
        return true;
    char path[64];
    snprintf(path, sizeof(path), "/run/dhcp/%s.lease", f->name);
    return util_exists(path);
}

static int load(struct net_if_info *list, int cap)
{
    int n = pc_net_if_list(list, (uint32_t)cap);
    if (n < 0)
        util_err("cannot read interfaces");
    return n;
}

static const struct net_if_info *find(const struct net_if_info *list, int n, const char *name)
{
    for (int i = 0; i < n; i++)
        if (strcmp(list[i].name, name) == 0)
            return &list[i];
    return 0;
}

static int show_link(bool addresses)
{
    struct net_if_info list[NET_IF_MAX_COUNT];
    int n = load(list, NET_IF_MAX_COUNT);
    for (int i = 0; i < n; i++) {
        const struct net_if_info *f = &list[i];
        bool lo = strcmp(f->name, "lo") == 0;
        printf("%d: %s%s%s: <%s%s> mtu %u state %s%s%s\n", i + 1, paint(C_BOLD), f->name,
               paint(C_RESET), lo ? "LOOPBACK," : "BROADCAST,", f->link_up ? "UP" : "DOWN",
               f->mtu, paint(f->link_up ? C_GREEN : C_RED), f->link_up ? "UP" : "DOWN",
               paint(C_RESET));
        printf("    link/%s %02x:%02x:%02x:%02x:%02x:%02x\n", lo ? "loopback" : "ether", f->mac[0],
               f->mac[1], f->mac[2], f->mac[3], f->mac[4], f->mac[5]);
        if (!addresses)
            continue;
        if (f->has_ip) {
            char a[16];
            addr_text(f->ip_address, a, sizeof(a));
            printf("    inet %s%s/%d%s %s\n", paint(C_CYAN), a, prefix_of(f->netmask), paint(C_RESET),
                   lo ? "scope host" : (dynamic_lease(f) ? "dynamic" : "static"));
        }
        if (f->dns_server) {
            char d[16];
            addr_text(f->dns_server, d, sizeof(d));
            printf("    dns %s\n", d);
        }
        printf("    RX %llu packets %llu bytes, TX %llu packets %llu bytes, dropped %llu/%llu\n",
               (unsigned long long)f->rx_packets, (unsigned long long)f->rx_bytes,
               (unsigned long long)f->tx_packets, (unsigned long long)f->tx_bytes,
               (unsigned long long)f->rx_dropped, (unsigned long long)f->tx_dropped);
    }
    return 0;
}

static int show_route(void)
{
    struct net_if_info list[NET_IF_MAX_COUNT];
    int n = load(list, NET_IF_MAX_COUNT);
    for (int i = 0; i < n; i++) {
        const struct net_if_info *f = &list[i];
        if (!f->has_ip || !f->gateway)
            continue;
        char g[16];
        addr_text(f->gateway, g, sizeof(g));
        printf("default via %s dev %s%s\n", g, f->name, dynamic_lease(f) ? " proto dhcp" : "");
    }
    for (int i = 0; i < n; i++) {
        const struct net_if_info *f = &list[i];
        if (!f->has_ip)
            continue;
        char net[16], src[16];
        addr_text(f->ip_address & f->netmask, net, sizeof(net));
        addr_text(f->ip_address, src, sizeof(src));
        printf("%s/%d dev %s scope %s src %s\n", net, prefix_of(f->netmask), f->name,
               strcmp(f->name, "lo") == 0 ? "host" : "link", src);
    }
    return 0;
}

static int configure(const char *dev, uint32_t op, uint32_t address, uint32_t netmask,
                     uint32_t gateway, uint32_t dns)
{
    struct net_if_config c;
    memset(&c, 0, sizeof(c));
    snprintf(c.name, sizeof(c.name), "%s", dev);
    c.op = op;
    c.address = address;
    c.netmask = netmask;
    c.gateway = gateway;
    c.dns = dns;
    int32_t rc = pc_net_configure(&c);
    if (rc < 0) {
        errno = -rc;
        return util_err("%s: %s", dev, strerror(errno));
    }
    return 0;
}

static const char *arg_after(int argc, char **argv, const char *key)
{
    for (int i = 0; i + 1 < argc; i++)
        if (strcmp(argv[i], key) == 0)
            return argv[i + 1];
    return 0;
}

static int addr_cmd(int argc, char **argv)
{
    if (argc == 0 || strcmp(argv[0], "show") == 0 || strcmp(argv[0], "list") == 0)
        return show_link(true);
    const char *dev = arg_after(argc, argv, "dev");
    if (!dev)
        return util_err("missing \"dev NAME\"");
    struct net_if_info list[NET_IF_MAX_COUNT];
    int n = load(list, NET_IF_MAX_COUNT);
    const struct net_if_info *f = find(list, n, dev);
    if (!f)
        return util_err("%s: no such device", dev);
    if (strcmp(argv[0], "add") == 0 && argc >= 2) {
        char text[32];
        snprintf(text, sizeof(text), "%s", argv[1]);
        int prefix = 24;
        char *slash = strchr(text, '/');
        if (slash) {
            *slash = 0;
            prefix = atoi(slash + 1);
        }
        uint32_t address;
        if (!parse_addr(text, &address) || prefix < 1 || prefix > 32)
            return util_err("bad address %s", argv[1]);
        uint32_t mask = prefix == 32 ? 0xFFFFFFFFU : ~(0xFFFFFFFFU >> prefix);
        return configure(dev, NET_CFG_SET_IPV4, address, mask, f->has_ip ? f->gateway : 0, 0);
    }
    if (strcmp(argv[0], "flush") == 0 || strcmp(argv[0], "del") == 0)
        return configure(dev, NET_CFG_CLEAR_IPV4, 0, 0, 0, 0);
    return util_err("unknown addr command \"%s\"", argv[0]);
}

static int route_cmd(int argc, char **argv)
{
    if (argc == 0 || strcmp(argv[0], "show") == 0 || strcmp(argv[0], "list") == 0)
        return show_route();
    const char *via = arg_after(argc, argv, "via");
    const char *dev = arg_after(argc, argv, "dev");
    bool add = strcmp(argv[0], "add") == 0 || strcmp(argv[0], "replace") == 0;
    bool del = strcmp(argv[0], "del") == 0;
    if ((!add && !del) || argc < 2 || strcmp(argv[1], "default") != 0)
        return util_err("only \"ip route add|del default via GW dev NAME\" is supported");
    if (!dev)
        return util_err("missing \"dev NAME\"");
    struct net_if_info list[NET_IF_MAX_COUNT];
    int n = load(list, NET_IF_MAX_COUNT);
    const struct net_if_info *f = find(list, n, dev);
    if (!f || !f->has_ip)
        return util_err("%s: no IPv4 address", dev);
    uint32_t gateway = 0;
    if (add && (!via || !parse_addr(via, &gateway)))
        return util_err("missing or bad \"via GW\"");
    return configure(dev, NET_CFG_SET_IPV4, f->ip_address, f->netmask, gateway, 0);
}

static int dns_cmd(int argc, char **argv)
{
    const char *dev = arg_after(argc, argv, "dev");
    if (argc < 2 || strcmp(argv[0], "set") != 0 || !dev)
        return util_err("usage: ip dns set ADDRESS dev NAME");
    uint32_t dns;
    if (!parse_addr(argv[1], &dns))
        return util_err("bad address %s", argv[1]);
    return configure(dev, NET_CFG_SET_DNS, 0, 0, 0, dns);
}

static int dhcp_cmd(int argc, char **argv)
{
    const char *dev = arg_after(argc, argv, "dev");
    if (argc < 1 || !dev)
        return util_err("usage: ip dhcp start|stop dev NAME");
    if (strcmp(argv[0], "start") == 0)
        return configure(dev, NET_CFG_DHCP_START, 0, 0, 0, 0);
    if (strcmp(argv[0], "stop") == 0)
        return configure(dev, NET_CFG_DHCP_STOP, 0, 0, 0, 0);
    return util_err("usage: ip dhcp start|stop dev NAME");
}

static bool is(const char *word, const char *full)
{
    size_t n = strlen(word);
    return n && strncmp(full, word, n) == 0;
}

int main(int argc, char **argv)
{
    util_init(argc, argv);
    color = util_color(1, "auto");
    if (argc < 2 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        printf("usage: ip addr|link|route|dns|dhcp ...\n"
               "  ip a                                   show addresses\n"
               "  ip link                                show links\n"
               "  ip route                               show routes\n"
               "  ip addr add A.B.C.D/N dev NAME         set address\n"
               "  ip addr flush dev NAME                 remove address\n"
               "  ip route add default via GW dev NAME   set gateway\n"
               "  ip dns set A.B.C.D dev NAME            set DNS server\n"
               "  ip dhcp start|stop dev NAME            control DHCP\n");
        return argc < 2 ? 1 : 0;
    }
    int first = 1;
    while (first < argc && argv[first][0] == '-' && argv[first][1]) {
        if (strcmp(argv[first], "-c") == 0 || strcmp(argv[first], "-color") == 0)
            color = true;
        first++;
    }
    if (first >= argc)
        return util_err("missing object, try \"ip --help\"");
    const char *object = argv[first];
    int rest = argc - first - 1;
    char **args = argv + first + 1;
    if (is(object, "address"))
        return addr_cmd(rest, args);
    if (is(object, "link"))
        return show_link(false);
    if (is(object, "route"))
        return route_cmd(rest, args);
    if (strcmp(object, "dns") == 0)
        return dns_cmd(rest, args);
    if (strcmp(object, "dhcp") == 0)
        return dhcp_cmd(rest, args);
    return util_err("unknown object \"%s\", try \"ip --help\"", object);
}
