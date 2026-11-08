#pragma once
// PureC hosted libc: poll over console, pipe and file descriptors.

#ifdef __cplusplus
extern "C" {
#endif
typedef unsigned long nfds_t;

struct pollfd {
    int fd;
    short events;
    short revents;
};

#define POLLIN 0x001
#define POLLPRI 0x002
#define POLLOUT 0x004
#define POLLERR 0x008
#define POLLHUP 0x010
#define POLLNVAL 0x020
#define POLLRDNORM 0x040
#define POLLRDBAND 0x080
#define POLLWRNORM 0x100
#define POLLWRBAND 0x200

int poll(struct pollfd *fds, nfds_t count, int timeout);
#ifdef __cplusplus
}
#endif
