#pragma once
#include "netinet/in.h"
#include "sys/socket.h"

#define AI_PASSIVE 0x1
#define AI_CANONNAME 0x2
#define AI_NUMERICHOST 0x4
#define AI_V4MAPPED 0x8
#define AI_ALL 0x10
#define AI_ADDRCONFIG 0x20
#define AI_NUMERICSERV 0x400

#define NI_NUMERICHOST 1
#define NI_NUMERICSERV 2
#define NI_NOFQDN 4
#define NI_NAMEREQD 8
#define NI_DGRAM 16
#define NI_MAXHOST 1025
#define NI_MAXSERV 32

#define EAI_BADFLAGS -1
#define EAI_NONAME -2
#define EAI_AGAIN -3
#define EAI_FAIL -4
#define EAI_FAMILY -6
#define EAI_SOCKTYPE -7
#define EAI_SERVICE -8
#define EAI_MEMORY -10
#define EAI_SYSTEM -11
#define EAI_OVERFLOW -12

#define HOST_NOT_FOUND 1
#define TRY_AGAIN 2
#define NO_RECOVERY 3
#define NO_DATA 4

struct addrinfo {
    int ai_flags;
    int ai_family;
    int ai_socktype;
    int ai_protocol;
    socklen_t ai_addrlen;
    struct sockaddr *ai_addr;
    char *ai_canonname;
    struct addrinfo *ai_next;
};

struct hostent {
    char *h_name;
    char **h_aliases;
    int h_addrtype;
    int h_length;
    char **h_addr_list;
};
#define h_addr h_addr_list[0]

struct servent {
    char *s_name;
    char **s_aliases;
    int s_port;
    char *s_proto;
};

extern int h_errno;

#ifdef __cplusplus
extern "C" {
#endif

int getaddrinfo(const char *node, const char *service,
                const struct addrinfo *hints, struct addrinfo **result);
void freeaddrinfo(struct addrinfo *result);
const char *gai_strerror(int code);
int getnameinfo(const struct sockaddr *address, socklen_t length, char *host,
                socklen_t host_length, char *service, socklen_t service_length,
                int flags);
struct hostent *gethostbyname(const char *name);
const char *hstrerror(int code);
struct servent *getservbyname(const char *name, const char *protocol);
struct servent *getservbyport(int port, const char *protocol);

#ifdef __cplusplus
}
#endif
