#include "../kernel/syscall/syscall.h"
#include "include/hosted/arpa/inet.h"
#include "include/hosted/errno.h"
#include "include/hosted/netdb.h"
#include "include/hosted/netinet/in.h"
#include <stdint.h>
#include "include/hosted/stdio.h"
#include "include/hosted/stdlib.h"
#include "include/hosted/string.h"
#include "include/hosted/sys/socket.h"

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

ssize_t sendto(int fd, const void *buffer, size_t length, int flags,
               const struct sockaddr *address, socklen_t address_length)
{
    (void)address;
    (void)address_length;
    return send(fd, buffer, length, flags);
}

ssize_t recvfrom(int fd, void *buffer, size_t length, int flags,
                 struct sockaddr *address, socklen_t *address_length)
{
    ssize_t got = recv(fd, buffer, length, flags);
    if (got >= 0 && address && address_length)
        (void)getpeername(fd, address, address_length);
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
    (void)domain;
    (void)type;
    (void)protocol;
    (void)fds;
    errno = EOPNOTSUPP;
    return -1;
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
} services[] = {{"ftp", 21},   {"ssh", 22},  {"telnet", 23}, {"smtp", 25},
                {"domain", 53}, {"http", 80}, {"pop3", 110},  {"ntp", 123},
                {"imap", 143}, {"https", 443}, {"imaps", 993}, {"pop3s", 995}};

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
    for (unsigned i = 0; i < sizeof(services) / sizeof(services[0]); i++)
    {
        if (strcmp(services[i].name, service) == 0)
        {
            *port = services[i].port;
            return 0;
        }
    }
    return EAI_SERVICE;
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
    if (socktype != 0 && socktype != SOCK_STREAM)
        return EAI_SOCKTYPE;
    if (protocol != 0 && protocol != IPPROTO_TCP)
        return EAI_SOCKTYPE;
    if (!node && !service)
        return EAI_NONAME;
    int port;
    int status = parse_service(service, flags, &port);
    if (status)
        return status;

    uint32_t address_be;
    if (!node)
        address_be = (flags & AI_PASSIVE) ? htonl(INADDR_ANY)
                                          : htonl(INADDR_LOOPBACK);
    else if (strcmp(node, "localhost") == 0)
        address_be = htonl(INADDR_LOOPBACK);
    else
    {
        struct in_addr parsed;
        if (inet_aton(node, &parsed))
            address_be = parsed.s_addr;
        else if (flags & AI_NUMERICHOST)
            return EAI_NONAME;
        else
        {
            int64_t code = pc_syscall(SYS_DNS_RESOLVE, (uint64_t)(uintptr_t)node,
                                      5000, (uint64_t)(uintptr_t)&address_be);
            if (code == -ENOENT || code == -EINVAL)
                return EAI_NONAME;
            if (code == -ETIMEDOUT || code == -EINTR || code == -ENETUNREACH)
                return EAI_AGAIN;
            if (code < 0)
                return EAI_FAIL;
        }
    }

    struct addrinfo *info = calloc(1, sizeof(*info) + sizeof(struct sockaddr_in));
    if (!info)
        return EAI_MEMORY;
    struct sockaddr_in *sin = (struct sockaddr_in *)(info + 1);
    sin->sin_family = AF_INET;
    sin->sin_port = htons((uint16_t)port);
    sin->sin_addr.s_addr = address_be;
    info->ai_family = AF_INET;
    info->ai_socktype = SOCK_STREAM;
    info->ai_protocol = IPPROTO_TCP;
    info->ai_addrlen = sizeof(*sin);
    info->ai_addr = (struct sockaddr *)sin;
    if ((flags & AI_CANONNAME) && node)
        info->ai_canonname = strdup(node);
    *result = info;
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
    static struct servent entry;
    static char service_name[16];
    static char proto[8] = "tcp";
    static char *aliases[1];
    if (protocol && strcmp(protocol, "tcp") != 0)
        return 0;
    for (unsigned i = 0; i < sizeof(services) / sizeof(services[0]); i++)
    {
        if (strcmp(services[i].name, name) == 0)
        {
            strncpy(service_name, name, sizeof(service_name) - 1);
            entry.s_name = service_name;
            entry.s_aliases = aliases;
            entry.s_port = htons((uint16_t)services[i].port);
            entry.s_proto = proto;
            return &entry;
        }
    }
    return 0;
}
