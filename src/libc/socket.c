#include "../kernel/syscall/syscall.h"
#include "include/hosted/arpa/inet.h"
#include "include/hosted/errno.h"
#include "include/hosted/netdb.h"
#include "include/hosted/netinet/in.h"
#include <stdint.h>
#include "include/hosted/stdio.h"
#include "include/hosted/stdlib.h"
#include "include/hosted/string.h"
#include "include/hosted/strings.h"
#include "include/hosted/sys/time.h"
#include "../libc/include/purec.h"
#include "include/hosted/sys/socket.h"
#include "include/hosted/sys/uio.h"
#include "include/hosted/unistd.h"

int64_t pc_syscall(uint64_t number, uint64_t argument1, uint64_t argument2,
                   uint64_t argument3);

int h_errno;

static int64_t syscall_four(uint64_t number, uint64_t argument1,
                            uint64_t argument2, uint64_t argument3,
                            uint64_t argument4)
{
    int64_t result;
    __asm__ volatile("int $0x80"
                     : "=a"(result)
                     : "a"(number), "b"(argument1), "c"(argument2),
                       "d"(argument3), "S"(argument4)
                     : "r10", "r8", "memory");
    return result;
}

static int result_of(int64_t value)
{
    if (value < 0)
    {
        errno = (int)-value;
        return -1;
    }
    return (int)value;
}

static ssize_t size_result_of(int64_t value)
{
    if (value < 0)
    {
        errno = (int)-value;
        return -1;
    }
    return (ssize_t)value;
}

int socket(int domain, int type, int protocol)
{
    return result_of(pc_syscall(SYS_SOCKET, (uint64_t)(uint32_t)domain,
                                (uint64_t)(uint32_t)type,
                                (uint64_t)(uint32_t)protocol));
}

int connect(int fd, const struct sockaddr *address, socklen_t length)
{
    if (!address)
    {
        errno = EFAULT;
        return -1;
    }
    return result_of(pc_syscall(SYS_CONNECT, (uint64_t)fd,
                                (uint64_t)(uintptr_t)address, length));
}

int bind(int fd, const struct sockaddr *address, socklen_t length)
{
    if (!address)
    {
        errno = EFAULT;
        return -1;
    }
    return result_of(pc_syscall(SYS_BIND, (uint64_t)fd,
                                (uint64_t)(uintptr_t)address, length));
}

int listen(int fd, int backlog)
{
    return result_of(pc_syscall(SYS_LISTEN, (uint64_t)fd,
                                (uint64_t)(uint32_t)backlog, 0));
}

int accept4(int fd, struct sockaddr *address, socklen_t *length, int flags)
{
    return result_of(syscall_four(SYS_ACCEPT, (uint64_t)fd,
                                  (uint64_t)(uintptr_t)address,
                                  (uint64_t)(uintptr_t)length,
                                  (uint64_t)(uint32_t)flags));
}

int accept(int fd, struct sockaddr *address, socklen_t *length)
{
    return accept4(fd, address, length, 0);
}

int shutdown(int fd, int how)
{
    return result_of(pc_syscall(SYS_SOCK_SHUTDOWN, (uint64_t)fd,
                                (uint64_t)(uint32_t)how, 0));
}

ssize_t send(int fd, const void *buffer, size_t length, int flags)
{
    return size_result_of(syscall_four(SYS_SOCK_SEND, (uint64_t)fd,
                                       (uint64_t)(uintptr_t)buffer, length,
                                       (uint64_t)(uint32_t)flags));
}

ssize_t recv(int fd, void *buffer, size_t length, int flags)
{
    return size_result_of(syscall_four(SYS_SOCK_RECV, (uint64_t)fd,
                                       (uint64_t)(uintptr_t)buffer, length,
                                       (uint64_t)(uint32_t)flags));
}

static int64_t syscall_five(uint64_t number, uint64_t argument1,
                            uint64_t argument2, uint64_t argument3,
                            uint64_t argument4, uint64_t argument5)
{
    int64_t result;
    __asm__ volatile("int $0x80"
                     : "=a"(result)
                     : "a"(number), "b"(argument1), "c"(argument2),
                       "d"(argument3), "S"(argument4), "D"(argument5)
                     : "r10", "r8", "memory");
    return result;
}

ssize_t sendto(int fd, const void *buffer, size_t length, int flags,
               const struct sockaddr *address, socklen_t address_length)
{
    if (address && address_length < (socklen_t)sizeof(struct sockaddr_in))
    {
        errno = EINVAL;
        return -1;
    }
    return size_result_of(syscall_five(SYS_SENDTO, (uint64_t)fd,
                                       (uint64_t)(uintptr_t)buffer, length,
                                       (uint64_t)(uint32_t)flags,
                                       (uint64_t)(uintptr_t)address));
}

ssize_t recvfrom(int fd, void *buffer, size_t length, int flags,
                 struct sockaddr *address, socklen_t *address_length)
{
    struct sockaddr_in source;
    memset(&source, 0, sizeof(source));
    ssize_t got = size_result_of(syscall_five(SYS_RECVFROM, (uint64_t)fd,
                                              (uint64_t)(uintptr_t)buffer, length,
                                              (uint64_t)(uint32_t)flags,
                                              (uint64_t)(uintptr_t)&source));
    if (got >= 0 && address && address_length)
    {
        socklen_t copy = *address_length < (socklen_t)sizeof(source)
                             ? *address_length
                             : (socklen_t)sizeof(source);
        memcpy(address, &source, copy);
        *address_length = sizeof(source);
    }
    return got;
}

int getsockname(int fd, struct sockaddr *address, socklen_t *length)
{
    return result_of(syscall_four(SYS_SOCKNAME, (uint64_t)fd,
                                  (uint64_t)(uintptr_t)address,
                                  (uint64_t)(uintptr_t)length, 0));
}

int getpeername(int fd, struct sockaddr *address, socklen_t *length)
{
    return result_of(syscall_four(SYS_SOCKNAME, (uint64_t)fd,
                                  (uint64_t)(uintptr_t)address,
                                  (uint64_t)(uintptr_t)length, 1));
}

int setsockopt(int fd, int level, int name, const void *value, socklen_t length)
{
    uint64_t key = ((uint64_t)(uint32_t)level << 32) | (uint32_t)name;
    return result_of(syscall_four(SYS_SETSOCKOPT, (uint64_t)fd, key,
                                  (uint64_t)(uintptr_t)value, length));
}

int getsockopt(int fd, int level, int name, void *value, socklen_t *length)
{
    uint64_t key = ((uint64_t)(uint32_t)level << 32) | (uint32_t)name;
    return result_of(syscall_four(SYS_GETSOCKOPT, (uint64_t)fd, key,
                                  (uint64_t)(uintptr_t)value,
                                  (uint64_t)(uintptr_t)length));
}

int socketpair(int domain, int type, int protocol, int fds[2])
{
    if (!fds)
    {
        errno = EFAULT;
        return -1;
    }
    return result_of(syscall_four(SYS_SOCKETPAIR, (uint64_t)(uint32_t)domain,
                                  (uint64_t)(uint32_t)type,
                                  (uint64_t)(uint32_t)protocol,
                                  (uint64_t)(uintptr_t)fds));
}

ssize_t sendmsg(int fd, const struct msghdr *message, int flags)
{
    if (!message)
    {
        errno = EFAULT;
        return -1;
    }
    return size_result_of(pc_syscall(SYS_SENDMSG, (uint64_t)(uint32_t)fd,
                                     (uint64_t)(uintptr_t)message,
                                     (uint64_t)(uint32_t)flags));
}

ssize_t recvmsg(int fd, struct msghdr *message, int flags)
{
    if (!message)
    {
        errno = EFAULT;
        return -1;
    }
    return size_result_of(pc_syscall(SYS_RECVMSG, (uint64_t)(uint32_t)fd,
                                     (uint64_t)(uintptr_t)message,
                                     (uint64_t)(uint32_t)flags));
}

ssize_t readv(int fd, const struct iovec *iov, int count)
{
    if (count < 0 || count > UIO_MAXIOV)
    {
        errno = EINVAL;
        return -1;
    }
    ssize_t total = 0;
    for (int i = 0; i < count; i++)
    {
        if (!iov[i].iov_len)
            continue;
        ssize_t got = read(fd, iov[i].iov_base, iov[i].iov_len);
        if (got < 0)
            return total ? total : -1;
        total += got;
        if ((size_t)got < iov[i].iov_len)
            break;
    }
    return total;
}

ssize_t writev(int fd, const struct iovec *iov, int count)
{
    if (count < 0 || count > UIO_MAXIOV)
    {
        errno = EINVAL;
        return -1;
    }
    ssize_t total = 0;
    for (int i = 0; i < count; i++)
    {
        if (!iov[i].iov_len)
            continue;
        ssize_t put = write(fd, iov[i].iov_base, iov[i].iov_len);
        if (put < 0)
            return total ? total : -1;
        total += put;
        if ((size_t)put < iov[i].iov_len)
            break;
    }
    return total;
}

int inet_aton(const char *text, struct in_addr *address)
{
    uint32_t value = 0;
    if (!text)
        return 0;
    for (int part = 0; part < 4; part++)
    {
        if (*text < '0' || *text > '9')
            return 0;
        uint32_t octet = 0;
        int digits = 0;
        while (*text >= '0' && *text <= '9')
        {
            octet = octet * 10 + (uint32_t)(*text - '0');
            if (octet > 255 || ++digits > 3)
                return 0;
            text++;
        }
        value = (value << 8) | octet;
        if (part < 3)
        {
            if (*text != '.')
                return 0;
            text++;
        }
    }
    if (*text)
        return 0;
    if (address)
        address->s_addr = htonl(value);
    return 1;
}

in_addr_t inet_addr(const char *text)
{
    struct in_addr address;
    return inet_aton(text, &address) ? address.s_addr : INADDR_NONE;
}

static char *format_v4(uint32_t be, char *out)
{
    const uint8_t *b = (const uint8_t *)&be;
    snprintf(out, INET_ADDRSTRLEN, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
    return out;
}

char *inet_ntoa(struct in_addr address)
{
    static char buffer[INET_ADDRSTRLEN];
    return format_v4(address.s_addr, buffer);
}

int inet_pton(int family, const char *text, void *destination)
{
    if (family != AF_INET)
    {
        errno = EAFNOSUPPORT;
        return -1;
    }
    struct in_addr address;
    if (!inet_aton(text, &address))
        return 0;
    memcpy(destination, &address, sizeof(address));
    return 1;
}

const char *inet_ntop(int family, const void *source, char *destination,
                      socklen_t size)
{
    if (family != AF_INET)
    {
        errno = EAFNOSUPPORT;
        return 0;
    }
    if (size < INET_ADDRSTRLEN)
    {
        char tmp[INET_ADDRSTRLEN];
        uint32_t be;
        memcpy(&be, source, sizeof(be));
        format_v4(be, tmp);
        if (strlen(tmp) + 1 > size)
        {
            errno = ENOSPC;
            return 0;
        }
        strcpy(destination, tmp);
        return destination;
    }
    uint32_t be;
    memcpy(&be, source, sizeof(be));
    return format_v4(be, destination);
}

static const struct
{
    const char *name;
    int port;
    const char *proto;
} services[] = {{"ftp", 21, "tcp"},     {"ssh", 22, "tcp"},     {"telnet", 23, "tcp"},
                {"smtp", 25, "tcp"},    {"domain", 53, "udp"},  {"domain", 53, "tcp"},
                {"bootps", 67, "udp"},  {"bootpc", 68, "udp"},  {"http", 80, "tcp"},
                {"pop3", 110, "tcp"},   {"ntp", 123, "udp"},    {"imap", 143, "tcp"},
                {"https", 443, "tcp"},  {"imaps", 993, "tcp"},  {"pop3s", 995, "tcp"}};

static struct servent serv_entry;
static char serv_name[32];
static char serv_proto[8];
static char serv_alias_text[64];
static char *serv_aliases[4];

static struct servent *serv_fill(const char *name, int port, const char *proto,
                                 const char *aliases)
{
    strncpy(serv_name, name, sizeof(serv_name) - 1);
    serv_name[sizeof(serv_name) - 1] = 0;
    strncpy(serv_proto, proto, sizeof(serv_proto) - 1);
    serv_proto[sizeof(serv_proto) - 1] = 0;
    serv_alias_text[0] = 0;
    if (aliases)
    {
        strncpy(serv_alias_text, aliases, sizeof(serv_alias_text) - 1);
        serv_alias_text[sizeof(serv_alias_text) - 1] = 0;
    }
    unsigned n = 0;
    char *cursor = serv_alias_text;
    while (*cursor && n + 1 < sizeof(serv_aliases) / sizeof(serv_aliases[0]))
    {
        while (*cursor == ' ' || *cursor == '\t')
            *cursor++ = 0;
        if (!*cursor)
            break;
        serv_aliases[n++] = cursor;
        while (*cursor && *cursor != ' ' && *cursor != '\t')
            cursor++;
    }
    serv_aliases[n] = 0;
    serv_entry.s_name = serv_name;
    serv_entry.s_aliases = serv_aliases;
    serv_entry.s_port = htons((uint16_t)port);
    serv_entry.s_proto = serv_proto;
    return &serv_entry;
}

static bool word_in(const char *list, const char *word)
{
    size_t n = strlen(word);
    while (*list)
    {
        while (*list == ' ' || *list == '\t')
            list++;
        if (strncmp(list, word, n) == 0 &&
            (list[n] == 0 || list[n] == ' ' || list[n] == '\t'))
            return true;
        while (*list && *list != ' ' && *list != '\t')
            list++;
    }
    return false;
}

static struct servent *serv_lookup(const char *name, int port, const char *proto)
{
    FILE *file = fopen("/etc/services", "r");
    if (file)
    {
        char line[256];
        while (fgets(line, sizeof(line), file))
        {
            char *hash = strchr(line, '#');
            if (hash)
                *hash = 0;
            char *nl = strchr(line, '\n');
            if (nl)
                *nl = 0;
            char entry_name[32], entry_port[24];
            int consumed = 0;
            if (sscanf(line, "%31s %23s %n", entry_name, entry_port, &consumed) < 2)
                continue;
            char *slash = strchr(entry_port, '/');
            if (!slash)
                continue;
            *slash = 0;
            int entry_number = atoi(entry_port);
            const char *entry_proto = slash + 1;
            const char *aliases = line + consumed;
            if (proto && strcmp(proto, entry_proto) != 0)
                continue;
            bool match = name ? (strcmp(name, entry_name) == 0 || word_in(aliases, name))
                              : entry_number == port;
            if (!match)
                continue;
            fclose(file);
            return serv_fill(entry_name, entry_number, entry_proto, aliases);
        }
        fclose(file);
    }
    for (unsigned i = 0; i < sizeof(services) / sizeof(services[0]); i++)
    {
        if (proto && strcmp(proto, services[i].proto) != 0)
            continue;
        if (name ? strcmp(name, services[i].name) == 0 : services[i].port == port)
            return serv_fill(services[i].name, services[i].port, services[i].proto, 0);
    }
    return 0;
}

static int parse_service(const char *service, int flags, int *port)
{
    if (!service || !*service)
    {
        *port = 0;
        return 0;
    }
    char *end;
    unsigned long value = strtoul(service, &end, 10);
    if (*end == '\0')
    {
        if (value > 65535)
            return EAI_SERVICE;
        *port = (int)value;
        return 0;
    }
    if (flags & AI_NUMERICSERV)
        return EAI_NONAME;
    struct servent *entry = serv_lookup(service, 0, 0);
    if (!entry)
        return EAI_SERVICE;
    *port = ntohs((uint16_t)entry->s_port);
    return 0;
}

#define RESOLVE_MAX 8

static int hosts_lookup(const char *name, uint32_t *out, int max)
{
    FILE *f = fopen("/etc/hosts", "r");
    if (!f)
        return 0;
    char line[256];
    int n = 0;
    while (n < max && fgets(line, sizeof(line), f))
    {
        char *hash = strchr(line, '#');
        if (hash)
            *hash = 0;
        char *save = 0;
        char *tok = strtok_r(line, " \t\r\n", &save);
        struct in_addr a;
        if (!tok || !inet_aton(tok, &a))
            continue;
        while ((tok = strtok_r(0, " \t\r\n", &save)))
            if (strcasecmp(tok, name) == 0)
            {
                out[n++] = a.s_addr;
                break;
            }
    }
    fclose(f);
    return n;
}

static int resolv_servers(uint32_t *out, int max)
{
    FILE *f = fopen("/etc/resolv.conf", "r");
    if (!f)
        return 0;
    char line[128];
    int n = 0;
    while (n < max && fgets(line, sizeof(line), f))
    {
        char word[16], addr[64];
        struct in_addr a;
        if (sscanf(line, "%15s %63s", word, addr) == 2 && strcmp(word, "nameserver") == 0 &&
            inet_aton(addr, &a))
            out[n++] = a.s_addr;
    }
    fclose(f);
    return n;
}

static int dns_skip(const uint8_t *p, int len, int at)
{
    while (at < len)
    {
        uint8_t l = p[at];
        if (!l)
            return at + 1;
        if ((l & 0xC0) == 0xC0)
            return at + 2;
        at += l + 1;
    }
    return -1;
}

static int dns_query(uint32_t server_be, const char *name, uint32_t *out, int max, int *rcode)
{
    uint8_t q[300];
    uint16_t id;
    if (pc_getrandom(&id, sizeof(id)) != (int32_t)sizeof(id))
        id = (uint16_t)getpid();
    memset(q, 0, 12);
    q[0] = (uint8_t)(id >> 8);
    q[1] = (uint8_t)id;
    q[2] = 0x01;
    q[5] = 1;
    int at = 12;
    const char *label = name;
    while (*label)
    {
        const char *dot = strchr(label, '.');
        size_t l = dot ? (size_t)(dot - label) : strlen(label);
        if (l == 0 || l > 63 || at + (int)l + 6 > (int)sizeof(q))
            return -1;
        q[at++] = (uint8_t)l;
        memcpy(q + at, label, l);
        at += (int)l;
        label += l;
        if (*label == '.')
            label++;
    }
    q[at++] = 0;
    q[at++] = 0;
    q[at++] = 1;
    q[at++] = 0;
    q[at++] = 1;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return -1;
    struct timeval tv = {2, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    struct sockaddr_in to;
    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_port = htons(53);
    to.sin_addr.s_addr = server_be;
    int found = -1;
    for (int attempt = 0; attempt < 2 && found < 0; attempt++)
    {
        if (sendto(fd, q, (size_t)at, 0, (struct sockaddr *)&to, sizeof(to)) != at)
            break;
        uint8_t r[512];
        for (;;)
        {
            ssize_t got = recv(fd, r, sizeof(r), 0);
            if (got < 0)
                break;
            if (got < 12 || r[0] != q[0] || r[1] != q[1] || !(r[2] & 0x80))
                continue;
            *rcode = r[3] & 15;
            int qd = r[4] << 8 | r[5], an = r[6] << 8 | r[7], p = 12;
            for (int i = 0; i < qd && p > 0; i++)
                p = dns_skip(r, (int)got, p) + 4;
            found = 0;
            for (int i = 0; i < an && p > 0 && p < got && found < max; i++)
            {
                p = dns_skip(r, (int)got, p);
                if (p < 0 || p + 10 > got)
                    break;
                int type = r[p] << 8 | r[p + 1], klass = r[p + 2] << 8 | r[p + 3];
                int rdlen = r[p + 8] << 8 | r[p + 9];
                p += 10;
                if (type == 1 && klass == 1 && rdlen == 4 && p + 4 <= got)
                    memcpy(&out[found++], r + p, 4);
                p += rdlen;
            }
            break;
        }
    }
    close(fd);
    return found;
}

static int name_lookup(const char *node, uint32_t *out, int max)
{
    int n = hosts_lookup(node, out, max);
    if (n > 0)
        return n;
    if (strcmp(node, "localhost") == 0)
    {
        out[0] = htonl(INADDR_LOOPBACK);
        return 1;
    }
    uint32_t servers[3];
    int count = resolv_servers(servers, 3);
    bool answered = false;
    for (int i = 0; i < count; i++)
    {
        int rcode = -1;
        n = dns_query(servers[i], node, out, max, &rcode);
        if (n > 0)
            return n;
        if (n == 0 && (rcode == 0 || rcode == 3))
        {
            answered = true;
            break;
        }
    }
    if (answered)
        return -EAI_NONAME;
    struct net_if_info infos[NET_IF_MAX_COUNT];
    int found = pc_net_if_list(infos, NET_IF_MAX_COUNT);
    uint32_t tried[NET_IF_MAX_COUNT];
    int tried_count = 0;
    for (int i = 0; i < found && i < NET_IF_MAX_COUNT; i++)
    {
        uint32_t server = infos[i].dns_server;
        if (!server || (server >> 24) == 127)
            continue;
        bool seen = false;
        for (int k = 0; k < tried_count; k++)
            if (tried[k] == server)
                seen = true;
        if (seen)
            continue;
        tried[tried_count++] = server;
        int rcode = -1;
        n = dns_query(htonl(server), node, out, max, &rcode);
        if (n > 0)
            return n;
        if (n == 0 && (rcode == 0 || rcode == 3))
            return -EAI_NONAME;
    }
    return -EAI_AGAIN;
}

int getaddrinfo(const char *node, const char *service,
                const struct addrinfo *hints, struct addrinfo **result)
{
    int flags = hints ? hints->ai_flags : 0;
    int family = hints ? hints->ai_family : AF_UNSPEC;
    int socktype = hints ? hints->ai_socktype : 0;
    int protocol = hints ? hints->ai_protocol : 0;
    if (!result)
        return EAI_FAIL;
    if (family != AF_UNSPEC && family != AF_INET)
        return EAI_FAMILY;
    if (socktype != 0 && socktype != SOCK_STREAM && socktype != SOCK_DGRAM)
        return EAI_SOCKTYPE;
    if (protocol != 0 && protocol != IPPROTO_TCP && protocol != IPPROTO_UDP)
        return EAI_SOCKTYPE;
    if (!node && !service)
        return EAI_NONAME;
    int port;
    int status = parse_service(service, flags, &port);
    if (status)
        return status;
    if (!socktype)
        socktype = protocol == IPPROTO_UDP ? SOCK_DGRAM : SOCK_STREAM;
    if (!protocol)
        protocol = socktype == SOCK_DGRAM ? IPPROTO_UDP : IPPROTO_TCP;

    uint32_t addrs[RESOLVE_MAX];
    int count = 1;
    if (!node)
        addrs[0] = (flags & AI_PASSIVE) ? htonl(INADDR_ANY) : htonl(INADDR_LOOPBACK);
    else
    {
        struct in_addr parsed;
        if (inet_aton(node, &parsed))
            addrs[0] = parsed.s_addr;
        else if (flags & AI_NUMERICHOST)
            return EAI_NONAME;
        else
        {
            count = name_lookup(node, addrs, RESOLVE_MAX);
            if (count < 0)
                return -count;
            if (count == 0)
                return EAI_NONAME;
        }
    }

    struct addrinfo *head = 0, *tail = 0;
    for (int i = 0; i < count; i++)
    {
        struct addrinfo *info = calloc(1, sizeof(*info) + sizeof(struct sockaddr_in));
        if (!info)
        {
            freeaddrinfo(head);
            return EAI_MEMORY;
        }
        struct sockaddr_in *sin = (struct sockaddr_in *)(info + 1);
        sin->sin_family = AF_INET;
        sin->sin_port = htons((uint16_t)port);
        sin->sin_addr.s_addr = addrs[i];
        info->ai_family = AF_INET;
        info->ai_socktype = socktype;
        info->ai_protocol = protocol;
        info->ai_addrlen = sizeof(*sin);
        info->ai_addr = (struct sockaddr *)sin;
        if (!head && (flags & AI_CANONNAME) && node)
            info->ai_canonname = strdup(node);
        if (tail)
            tail->ai_next = info;
        else
            head = info;
        tail = info;
    }
    *result = head;
    return 0;
}

void freeaddrinfo(struct addrinfo *result)
{
    while (result)
    {
        struct addrinfo *next = result->ai_next;
        free(result->ai_canonname);
        free(result);
        result = next;
    }
}

const char *gai_strerror(int code)
{
    switch (code)
    {
    case 0:
        return "Success";
    case EAI_BADFLAGS:
        return "Bad value for ai_flags";
    case EAI_NONAME:
        return "Name or service not known";
    case EAI_AGAIN:
        return "Temporary failure in name resolution";
    case EAI_FAIL:
        return "Non-recoverable failure in name resolution";
    case EAI_FAMILY:
        return "ai_family not supported";
    case EAI_SOCKTYPE:
        return "ai_socktype not supported";
    case EAI_SERVICE:
        return "Servname not supported for ai_socktype";
    case EAI_MEMORY:
        return "Memory allocation failure";
    case EAI_SYSTEM:
        return "System error";
    case EAI_OVERFLOW:
        return "Argument buffer overflow";
    default:
        return "Unknown error";
    }
}

int getnameinfo(const struct sockaddr *address, socklen_t length, char *host,
                socklen_t host_length, char *service, socklen_t service_length,
                int flags)
{
    (void)flags;
    if (!address || length < sizeof(struct sockaddr_in) ||
        address->sa_family != AF_INET)
        return EAI_FAMILY;
    const struct sockaddr_in *sin = (const struct sockaddr_in *)address;
    if (host && host_length)
    {
        char text[INET_ADDRSTRLEN];
        format_v4(sin->sin_addr.s_addr, text);
        if (strlen(text) + 1 > host_length)
            return EAI_OVERFLOW;
        strcpy(host, text);
    }
    if (service && service_length)
    {
        char text[8];
        snprintf(text, sizeof(text), "%u", ntohs(sin->sin_port));
        if (strlen(text) + 1 > service_length)
            return EAI_OVERFLOW;
        strcpy(service, text);
    }
    return 0;
}

struct hostent *gethostbyname(const char *name)
{
    static struct hostent entry;
    static char canonical[256];
    static uint32_t address;
    static char *addresses[2];
    static char *aliases[1];
    struct addrinfo hints = {0};
    struct addrinfo *info = 0;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    int code = getaddrinfo(name, 0, &hints, &info);
    if (code)
    {
        h_errno = code == EAI_AGAIN ? TRY_AGAIN : HOST_NOT_FOUND;
        return 0;
    }
    address = ((struct sockaddr_in *)info->ai_addr)->sin_addr.s_addr;
    freeaddrinfo(info);
    strncpy(canonical, name, sizeof(canonical) - 1);
    canonical[sizeof(canonical) - 1] = '\0';
    addresses[0] = (char *)&address;
    addresses[1] = 0;
    aliases[0] = 0;
    entry.h_name = canonical;
    entry.h_aliases = aliases;
    entry.h_addrtype = AF_INET;
    entry.h_length = 4;
    entry.h_addr_list = addresses;
    return &entry;
}

const char *hstrerror(int code)
{
    switch (code)
    {
    case HOST_NOT_FOUND:
        return "Unknown host";
    case TRY_AGAIN:
        return "Host name lookup failure";
    case NO_RECOVERY:
        return "Unknown server error";
    case NO_DATA:
        return "No address associated with name";
    default:
        return "Resolver internal error";
    }
}

struct servent *getservbyname(const char *name, const char *protocol)
{
    return name ? serv_lookup(name, 0, protocol) : 0;
}

struct servent *getservbyport(int port, const char *protocol)
{
    return serv_lookup(0, ntohs((uint16_t)port), protocol);
}
