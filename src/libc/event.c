#include "../kernel/syscall/syscall.h"
#include "include/hosted/errno.h"
#include "include/hosted/sys/epoll.h"
#include "include/hosted/sys/eventfd.h"
#include "include/hosted/sys/timerfd.h"
#include "include/hosted/unistd.h"
#include <stdint.h>

int64_t pc_syscall(uint64_t number, uint64_t argument1, uint64_t argument2,
                   uint64_t argument3);

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

int eventfd(unsigned int initial, int flags)
{
    if (flags & ~(EFD_SEMAPHORE | EFD_NONBLOCK | EFD_CLOEXEC))
    {
        errno = EINVAL;
        return -1;
    }
    return result_of(pc_syscall(SYS_EVENTFD, initial, (uint64_t)(uint32_t)flags, 0));
}

int eventfd_read(int fd, eventfd_t *value)
{
    return read(fd, value, sizeof(*value)) == (ssize_t)sizeof(*value) ? 0 : -1;
}

int eventfd_write(int fd, eventfd_t value)
{
    return write(fd, &value, sizeof(value)) == (ssize_t)sizeof(value) ? 0 : -1;
}

int timerfd_create(int clock_id, int flags)
{
    if (flags & ~(TFD_NONBLOCK | TFD_CLOEXEC))
    {
        errno = EINVAL;
        return -1;
    }
    return result_of(pc_syscall(SYS_TIMERFD_CREATE, (uint64_t)(uint32_t)clock_id,
                                (uint64_t)(uint32_t)flags, 0));
}

int timerfd_settime(int fd, int flags, const struct itimerspec *value,
                    struct itimerspec *old_value)
{
    if (!value)
    {
        errno = EFAULT;
        return -1;
    }
    return result_of(syscall_four(SYS_TIMERFD_SETTIME, (uint64_t)(uint32_t)fd,
                                  (uint64_t)(uint32_t)flags,
                                  (uint64_t)(uintptr_t)value,
                                  (uint64_t)(uintptr_t)old_value));
}

int timerfd_gettime(int fd, struct itimerspec *value)
{
    if (!value)
    {
        errno = EFAULT;
        return -1;
    }
    return result_of(pc_syscall(SYS_TIMERFD_GETTIME, (uint64_t)(uint32_t)fd,
                                (uint64_t)(uintptr_t)value, 0));
}

int epoll_create1(int flags)
{
    if (flags & ~EPOLL_CLOEXEC)
    {
        errno = EINVAL;
        return -1;
    }
    return result_of(pc_syscall(SYS_EPOLL_CREATE, (uint64_t)(uint32_t)flags, 0, 0));
}

int epoll_create(int size)
{
    if (size <= 0)
    {
        errno = EINVAL;
        return -1;
    }
    return epoll_create1(0);
}

int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event)
{
    if (op != EPOLL_CTL_DEL && !event)
    {
        errno = EFAULT;
        return -1;
    }
    return result_of(syscall_four(SYS_EPOLL_CTL, (uint64_t)(uint32_t)epfd,
                                  (uint64_t)(uint32_t)op,
                                  (uint64_t)(uint32_t)fd,
                                  (uint64_t)(uintptr_t)event));
}

int epoll_wait(int epfd, struct epoll_event *events, int max_events,
               int timeout)
{
    if (max_events <= 0 || !events)
    {
        errno = max_events <= 0 ? EINVAL : EFAULT;
        return -1;
    }
    return result_of(syscall_four(SYS_EPOLL_WAIT, (uint64_t)(uint32_t)epfd,
                                  (uint64_t)(uintptr_t)events,
                                  (uint64_t)(uint32_t)max_events,
                                  (uint64_t)(int64_t)timeout));
}
