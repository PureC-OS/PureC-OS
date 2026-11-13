#include "../../libc/include/purec.h"
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int failures;

static void check(bool ok, const char *what)
{
    printf("%s  %s\n", ok ? "[ OK ]" : "[FAIL]", what);
    if (!ok)
        failures++;
}

static struct sockaddr_in at(uint32_t address, uint16_t port)
{
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(address);
    return a;
}

static void test_loopback(void)
{
    int server = socket(AF_INET, SOCK_DGRAM, 0);
    int client = socket(AF_INET, SOCK_DGRAM, 0);
    check(server >= 0 && client >= 0, "socket(AF_INET, SOCK_DGRAM)");
    struct sockaddr_in local = at(0x7F000001, 5555);
    check(bind(server, (struct sockaddr *)&local, sizeof(local)) == 0, "bind 127.0.0.1:5555");
    check(sendto(client, "ping", 4, 0, (struct sockaddr *)&local, sizeof(local)) == 4,
          "sendto 127.0.0.1:5555");
    char buf[64];
    struct sockaddr_in from;
    socklen_t len = sizeof(from);
    ssize_t got = recvfrom(server, buf, sizeof(buf), 0, (struct sockaddr *)&from, &len);
    check(got == 4 && memcmp(buf, "ping", 4) == 0, "recvfrom got \"ping\"");
    printf("       from %s:%u\n", inet_ntoa(from.sin_addr), ntohs(from.sin_port));
    check(sendto(server, "pong", 4, 0, (struct sockaddr *)&from, len) == 4, "reply to sender");
    got = recv(client, buf, sizeof(buf), 0);
    check(got == 4 && memcmp(buf, "pong", 4) == 0, "client got \"pong\"");
    struct sockaddr_in second = at(0x7F000001, 5555);
    int dup = socket(AF_INET, SOCK_DGRAM, 0);
    check(bind(dup, (struct sockaddr *)&second, sizeof(second)) < 0 && errno == EADDRINUSE,
          "second bind on 5555 -> EADDRINUSE");
    close(dup);
    close(server);
    close(client);
}

static void test_privileged(void)
{
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in a = at(0, 53);
    int rc = bind(s, (struct sockaddr *)&a, sizeof(a));
    if (getuid() == 0)
        check(rc == 0, "root: bind UDP 53 allowed");
    else
        check(rc < 0 && errno == EACCES, "user: bind UDP 53 -> EACCES");
    close(s);
}

static void test_services(void)
{
    struct servent *e = getservbyname("domain", "udp");
    check(e && ntohs((uint16_t)e->s_port) == 53, "getservbyname(domain, udp) = 53");
    e = getservbyport(htons(22), "tcp");
    check(e && strcmp(e->s_name, "ssh") == 0, "getservbyport(22, tcp) = ssh");
    e = getservbyname("www", "tcp");
    check(e && ntohs((uint16_t)e->s_port) == 80, "alias www = 80");
}

static uint32_t dns_server(void)
{
    struct net_if_info list[4];
    int32_t n = pc_net_if_list(list, 4);
    for (int32_t i = 0; i < n; i++)
        if (list[i].has_ip && list[i].dns_server)
            return list[i].dns_server;
    return 0;
}

static void test_dns(void)
{
    uint32_t server = dns_server();
    if (!server) {
        printf("[SKIP]  no DNS server from DHCP\n");
        return;
    }
    uint8_t q[64] = {0x12, 0x34, 0x01, 0x00, 0, 1, 0, 0, 0, 0, 0, 0};
    size_t n = 12;
    const char *labels[] = {"example", "com"};
    for (int i = 0; i < 2; i++) {
        size_t l = strlen(labels[i]);
        q[n++] = (uint8_t)l;
        memcpy(q + n, labels[i], l);
        n += l;
    }
    q[n++] = 0;
    q[n++] = 0; q[n++] = 1;
    q[n++] = 0; q[n++] = 1;
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    struct timeval tv = {3, 0};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    struct sockaddr_in to = at(server, 53);
    char text[16];
    struct in_addr sa = {htonl(server)};
    snprintf(text, sizeof(text), "%s", inet_ntoa(sa));
    printf("       DNS server %s\n", text);
    check(sendto(s, q, n, 0, (struct sockaddr *)&to, sizeof(to)) == (ssize_t)n,
          "sendto DNS server:53");
    uint8_t r[512];
    struct sockaddr_in from;
    socklen_t len = sizeof(from);
    ssize_t got = recvfrom(s, r, sizeof(r), 0, (struct sockaddr *)&from, &len);
    check(got > 12 && r[0] == 0x12 && r[1] == 0x34 && ntohs(from.sin_port) == 53,
          "DNS reply received from port 53");
    if (got > 12) {
        unsigned answers = (unsigned)(r[6] << 8 | r[7]);
        size_t p = n;
        for (unsigned i = 0; i < answers && p + 12 <= (size_t)got; i++) {
            if ((r[p] & 0xC0) == 0xC0)
                p += 2;
            else {
                while (p < (size_t)got && r[p])
                    p += r[p] + 1;
                p++;
            }
            uint16_t type = (uint16_t)(r[p] << 8 | r[p + 1]);
            uint16_t rdlen = (uint16_t)(r[p + 8] << 8 | r[p + 9]);
            p += 10;
            if (type == 1 && rdlen == 4 && p + 4 <= (size_t)got) {
                printf("       example.com A %u.%u.%u.%u\n", r[p], r[p + 1], r[p + 2], r[p + 3]);
                check(true, "parsed A record");
                break;
            }
            p += rdlen;
        }
    }
    close(s);
}

int main(void)
{
    printf("udptest (uid %d)\n", (int)getuid());
    test_services();
    test_loopback();
    test_privileged();
    test_dns();
    printf("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
