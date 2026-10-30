// PureC hosted libc: tty layer. termios/ioctl talk to the kernel console tty
// (SYS_TTY_IOCTL); poll and select wait on console, pipe and file fds
// (SYS_POLL).

#include "include/hosted/errno.h"
#include "include/hosted/poll.h"
#include "include/hosted/sys/ioctl.h"
#include "include/hosted/sys/select.h"
#include "include/hosted/termios.h"
#include "include/hosted/unistd.h"
#include "include/purec.h"
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define POLL_LIMIT 256
#define POLL_TIMEOUT_MAX 2147483647L

_Static_assert(sizeof(struct termios) == 56, "termios must match the kernel tty ABI");

static int syscall_result(int64_t result)
{
    if (result < 0)
    {
        errno = (int)-result;
        return -1;
    }
    return (int)result;
}

static int require_console(int fd)
{
    int64_t kind = pc_syscall(SYS_FD_CTL, (uint64_t)fd, FD_CTL_KIND, 0);
    if (kind < 0)
    {
        errno = EBADF;
        return -1;
    }
    if (kind != FD_KIND_CONSOLE)
    {
        errno = ENOTTY;
        return -1;
    }
    return 0;
}

int ioctl(int fd, unsigned long request, ...)
{
    va_list arguments;
    va_start(arguments, request);
    void *argument = va_arg(arguments, void *);
    va_end(arguments);
    if (require_console(fd) < 0)
        return -1;
    return syscall_result(pc_syscall(SYS_TTY_IOCTL, (uint64_t)request,
                                     (uint64_t)(uintptr_t)argument, 0));
}

int tcgetattr(int fd, struct termios *mode)
{
    if (!mode)
    {
        errno = EFAULT;
        return -1;
    }
    return ioctl(fd, TCGETS, mode);
}

int tcsetattr(int fd, int action, const struct termios *mode)
{
    if (!mode)
    {
        errno = EFAULT;
        return -1;
    }
    unsigned long request;
    switch (action)
    {
    case TCSANOW:
        request = TCSETS;
        break;
    case TCSADRAIN:
        request = TCSETSW;
        break;
    case TCSAFLUSH:
        request = TCSETSF;
        break;
    default:
        errno = EINVAL;
        return -1;
    }
    return ioctl(fd, request, mode);
}

int tcflush(int fd, int queue)
{
    if (queue != TCIFLUSH && queue != TCOFLUSH && queue != TCIOFLUSH)
    {
        errno = EINVAL;
        return -1;
    }
    return ioctl(fd, TCFLSH, (void *)(uintptr_t)queue);
}

int tcdrain(int fd) { return require_console(fd); }

int tcflow(int fd, int action)
{
    if (require_console(fd) < 0)
        return -1;
    if (action < TCOOFF || action > TCION)
    {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int tcsendbreak(int fd, int duration)
{
    (void)duration;
    return require_console(fd);
}

speed_t cfgetispeed(const struct termios *mode) { return mode->c_ispeed; }

speed_t cfgetospeed(const struct termios *mode) { return mode->c_ospeed; }

int cfsetispeed(struct termios *mode, speed_t speed)
{
    mode->c_ispeed = speed;
    return 0;
}

int cfsetospeed(struct termios *mode, speed_t speed)
{
    mode->c_ospeed = speed;
    return 0;
}

int cfsetspeed(struct termios *mode, speed_t speed)
{
    mode->c_ispeed = speed;
    mode->c_ospeed = speed;
    return 0;
}

void cfmakeraw(struct termios *mode)
{
    mode->c_iflag &= ~(tcflag_t)(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR |
                                 IGNCR | ICRNL | IXON);
    mode->c_oflag &= ~(tcflag_t)OPOST;
    mode->c_lflag &= ~(tcflag_t)(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    mode->c_cflag &= ~(tcflag_t)(CSIZE | PARENB);
    mode->c_cflag |= CS8;
    mode->c_cc[VMIN] = 1;
    mode->c_cc[VTIME] = 0;
}

int poll(struct pollfd *fds, nfds_t count, int timeout)
{
    if (count > POLL_LIMIT || (count && !fds))
    {
        errno = count > POLL_LIMIT ? EINVAL : EFAULT;
        return -1;
    }
    return syscall_result(pc_syscall(SYS_POLL, (uint64_t)(uintptr_t)fds,
                                     (uint64_t)count,
                                     (uint64_t)(int64_t)timeout));
}

static int select_timeout(long seconds, long fraction_ms)
{
    if (seconds < 0 || fraction_ms < 0)
        return 0;
    if (seconds > POLL_TIMEOUT_MAX / 1000 - 1)
        return (int)POLL_TIMEOUT_MAX;
    return (int)(seconds * 1000 + fraction_ms);
}

static int select_with_timeout(int count, fd_set *read_set, fd_set *write_set,
                               fd_set *error_set, int timeout)
{
    if (count < 0 || count > FD_SETSIZE)
    {
        errno = EINVAL;
        return -1;
    }
    struct pollfd descriptors[FD_SETSIZE];
    nfds_t used = 0;
    for (int fd = 0; fd < count; fd++)
    {
        short events = 0;
        if (read_set && FD_ISSET(fd, read_set))
            events |= POLLIN;
        if (write_set && FD_ISSET(fd, write_set))
            events |= POLLOUT;
        if (error_set && FD_ISSET(fd, error_set))
            events |= POLLPRI;
        if (!events)
            continue;
        descriptors[used].fd = fd;
        descriptors[used].events = events;
        descriptors[used].revents = 0;
        used++;
    }
    int ready = poll(descriptors, used, timeout);
    if (ready < 0)
        return -1;
    if (read_set)
        FD_ZERO(read_set);
    if (write_set)
        FD_ZERO(write_set);
    if (error_set)
        FD_ZERO(error_set);
    int total = 0;
    for (nfds_t i = 0; i < used; i++)
    {
        short events = descriptors[i].events;
        short revents = descriptors[i].revents;
        if (revents & POLLNVAL)
        {
            errno = EBADF;
            return -1;
        }
        int fd = descriptors[i].fd;
        if (read_set && (events & POLLIN) &&
            (revents & (POLLIN | POLLHUP | POLLERR)))
        {
            FD_SET(fd, read_set);
            total++;
        }
        if (write_set && (events & POLLOUT) &&
            (revents & (POLLOUT | POLLERR)))
        {
            FD_SET(fd, write_set);
            total++;
        }
    }
    return total;
}

int select(int count, fd_set *read_set, fd_set *write_set, fd_set *error_set,
           struct timeval *timeout)
{
    int milliseconds = -1;
    if (timeout)
        milliseconds = select_timeout(timeout->tv_sec,
                                      (timeout->tv_usec + 999) / 1000);
    return select_with_timeout(count, read_set, write_set, error_set,
                               milliseconds);
}

int pselect(int count, fd_set *read_set, fd_set *write_set, fd_set *error_set,
            const struct timespec *timeout, const void *mask)
{
    (void)mask;
    int milliseconds = -1;
    if (timeout)
        milliseconds = select_timeout(timeout->tv_sec,
                                      (timeout->tv_nsec + 999999) / 1000000);
    return select_with_timeout(count, read_set, write_set, error_set,
                               milliseconds);
}
