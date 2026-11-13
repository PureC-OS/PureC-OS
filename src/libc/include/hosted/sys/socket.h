#ifndef PUREC_HOSTED_SYS_SOCKET_H
#define PUREC_HOSTED_SYS_SOCKET_H
#include <stddef.h>
#include <stdint.h>
#include "types.h"
#include "uio.h"

typedef unsigned int socklen_t;
typedef unsigned short sa_family_t;

#define AF_UNSPEC 0
#define AF_UNIX 1
#define AF_LOCAL AF_UNIX
#define AF_INET 2
#define AF_INET6 10
#define PF_UNSPEC AF_UNSPEC
#define PF_UNIX AF_UNIX
#define PF_LOCAL AF_UNIX
#define PF_INET AF_INET
#define PF_INET6 AF_INET6

#define SOCK_STREAM 1
#define SOCK_DGRAM 2
#define SOCK_RAW 3
#define SOCK_NONBLOCK 04000
#define SOCK_CLOEXEC 02000000

#define SOL_SOCKET 1
#define SOMAXCONN 4

#define SO_DEBUG 1
#define SO_PASSCRED 16
#define SO_PEERCRED 17
#define SO_ACCEPTCONN 30
#define SO_REUSEADDR 2
#define SO_TYPE 3
#define SO_ERROR 4
#define SO_DONTROUTE 5
#define SO_BROADCAST 6
#define SO_SNDBUF 7
#define SO_RCVBUF 8
#define SO_KEEPALIVE 9
#define SO_OOBINLINE 10
#define SO_LINGER 13
#define SO_REUSEPORT 15
#define SO_RCVLOWAT 18
#define SO_SNDLOWAT 19
#define SO_RCVTIMEO 20
#define SO_SNDTIMEO 21
#define SO_BINDTODEVICE 25

#define MSG_OOB 0x1
#define MSG_PEEK 0x2
#define MSG_DONTROUTE 0x4
#define MSG_WAITALL 0x100
#define MSG_DONTWAIT 0x40
#define MSG_NOSIGNAL 0x4000
#define MSG_TRUNC 0x20
#define MSG_CTRUNC 0x8
#define MSG_CMSG_CLOEXEC 0x40000000

#define SCM_RIGHTS 1
#define SCM_CREDENTIALS 2

#define SHUT_RD 0
#define SHUT_WR 1
#define SHUT_RDWR 2

struct sockaddr {
    sa_family_t sa_family;
    char sa_data[14];
};

struct sockaddr_storage {
    sa_family_t ss_family;
    char ss_pad[126];
};

struct msghdr {
    void *msg_name;
    socklen_t msg_namelen;
    struct iovec *msg_iov;
    size_t msg_iovlen;
    void *msg_control;
    size_t msg_controllen;
    int msg_flags;
};

struct cmsghdr {
    size_t cmsg_len;
    int cmsg_level;
    int cmsg_type;
};

struct ucred {
    int pid;
    unsigned int uid;
    unsigned int gid;
};

#define CMSG_ALIGN(length) (((length) + sizeof(size_t) - 1) & ~(sizeof(size_t) - 1))
#define CMSG_DATA(cmsg) ((unsigned char *)((struct cmsghdr *)(cmsg) + 1))
#define CMSG_LEN(length) (CMSG_ALIGN(sizeof(struct cmsghdr)) + (length))
#define CMSG_SPACE(length) (CMSG_ALIGN(sizeof(struct cmsghdr)) + CMSG_ALIGN(length))
#define CMSG_FIRSTHDR(msg) \
    ((msg)->msg_controllen >= sizeof(struct cmsghdr) ? (struct cmsghdr *)(msg)->msg_control : (struct cmsghdr *)0)
#define CMSG_NXTHDR(msg, cmsg) __cmsg_nxthdr((msg), (cmsg))

struct linger {
    int l_onoff;
    int l_linger;
};

#ifdef __cplusplus
extern "C" {
#endif

int socket(int domain, int type, int protocol);
int connect(int fd, const struct sockaddr *address, socklen_t length);
int bind(int fd, const struct sockaddr *address, socklen_t length);
int listen(int fd, int backlog);
int accept(int fd, struct sockaddr *address, socklen_t *length);
int accept4(int fd, struct sockaddr *address, socklen_t *length, int flags);
int shutdown(int fd, int how);
ssize_t send(int fd, const void *buffer, size_t length, int flags);
ssize_t recv(int fd, void *buffer, size_t length, int flags);
ssize_t sendto(int fd, const void *buffer, size_t length, int flags,
               const struct sockaddr *address, socklen_t address_length);
ssize_t recvfrom(int fd, void *buffer, size_t length, int flags,
                 struct sockaddr *address, socklen_t *address_length);
int getsockname(int fd, struct sockaddr *address, socklen_t *length);
int getpeername(int fd, struct sockaddr *address, socklen_t *length);
int setsockopt(int fd, int level, int name, const void *value, socklen_t length);
int getsockopt(int fd, int level, int name, void *value, socklen_t *length);
int socketpair(int domain, int type, int protocol, int fds[2]);
ssize_t sendmsg(int fd, const struct msghdr *message, int flags);
ssize_t recvmsg(int fd, struct msghdr *message, int flags);

static inline struct cmsghdr *__cmsg_nxthdr(struct msghdr *message, struct cmsghdr *cmsg)
{
    if (cmsg->cmsg_len < sizeof(struct cmsghdr))
        return (struct cmsghdr *)0;
    unsigned char *next = (unsigned char *)cmsg + CMSG_ALIGN(cmsg->cmsg_len);
    unsigned char *end = (unsigned char *)message->msg_control + message->msg_controllen;
    if (next + sizeof(struct cmsghdr) > end)
        return (struct cmsghdr *)0;
    return (struct cmsghdr *)next;
}

#ifdef __cplusplus
}
#endif

#endif
