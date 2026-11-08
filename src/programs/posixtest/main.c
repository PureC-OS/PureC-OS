#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int failures;

static void check(const char *name, int ok)
{
    printf("posixtest %-32s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok)
        failures++;
}

static long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static void test_eventfd(void)
{
    int fd = eventfd(0, EFD_NONBLOCK);
    uint64_t value = 0;
    check("eventfd create", fd >= 0);
    check("eventfd empty EAGAIN",
          read(fd, &value, sizeof(value)) < 0 && errno == EAGAIN);
    value = 5;
    check("eventfd write", write(fd, &value, sizeof(value)) == 8);
    value = 3;
    check("eventfd write add", write(fd, &value, sizeof(value)) == 8);
    value = 0;
    check("eventfd read sum", read(fd, &value, sizeof(value)) == 8 && value == 8);
    check("eventfd drained",
          read(fd, &value, sizeof(value)) < 0 && errno == EAGAIN);
    check("eventfd short buffer",
          read(fd, &value, 4) < 0 && errno == EINVAL);
    close(fd);

    fd = eventfd(3, EFD_NONBLOCK | EFD_SEMAPHORE);
    int ones = 0;
    while (read(fd, &value, sizeof(value)) == 8 && value == 1)
        ones++;
    check("eventfd semaphore", ones == 3);
    close(fd);
}

static void test_timerfd(void)
{
    int fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    uint64_t expirations = 0;
    check("timerfd create", fd >= 0);
    check("timerfd unarmed EAGAIN",
          read(fd, &expirations, sizeof(expirations)) < 0 && errno == EAGAIN);

    struct itimerspec spec;
    memset(&spec, 0, sizeof(spec));
    spec.it_value.tv_nsec = 50 * 1000000L;
    check("timerfd settime", timerfd_settime(fd, 0, &spec, 0) == 0);
    check("timerfd not yet",
          read(fd, &expirations, sizeof(expirations)) < 0 && errno == EAGAIN);
    struct pollfd pfd = {fd, POLLIN, 0};
    long start = now_ms();
    int ready = poll(&pfd, 1, 1000);
    long elapsed = now_ms() - start;
    check("timerfd poll fires", ready == 1 && (pfd.revents & POLLIN));
    check("timerfd delay", elapsed >= 40 && elapsed < 400);
    check("timerfd read one",
          read(fd, &expirations, sizeof(expirations)) == 8 && expirations == 1);

    spec.it_value.tv_nsec = 10 * 1000000L;
    spec.it_interval.tv_nsec = 10 * 1000000L;
    timerfd_settime(fd, 0, &spec, 0);
    struct timespec nap = {0, 100 * 1000000L};
    nanosleep(&nap, 0);
    expirations = 0;
    check("timerfd periodic",
          read(fd, &expirations, sizeof(expirations)) == 8 && expirations >= 5);
    struct itimerspec current;
    check("timerfd gettime",
          timerfd_gettime(fd, &current) == 0 && current.it_interval.tv_nsec == 10 * 1000000L);
    memset(&spec, 0, sizeof(spec));
    timerfd_settime(fd, 0, &spec, 0);
    nanosleep(&nap, 0);
    check("timerfd disarm",
          read(fd, &expirations, sizeof(expirations)) < 0 && errno == EAGAIN);
    close(fd);

    fd = timerfd_create(CLOCK_MONOTONIC, 0);
    memset(&spec, 0, sizeof(spec));
    spec.it_value.tv_nsec = 30 * 1000000L;
    timerfd_settime(fd, 0, &spec, 0);
    start = now_ms();
    expirations = 0;
    check("timerfd blocking read",
          read(fd, &expirations, sizeof(expirations)) == 8 && expirations == 1);
    elapsed = now_ms() - start;
    check("timerfd blocking delay", elapsed >= 20 && elapsed < 400);
    close(fd);
}

static void test_epoll(void)
{
    int ep = epoll_create1(0);
    check("epoll create", ep >= 0);
    int pipefd[2];
    pipe(pipefd);
    int ev = eventfd(0, EFD_NONBLOCK);

    struct epoll_event event;
    struct epoll_event out[8];
    event.events = EPOLLIN;
    event.data.fd = pipefd[0];
    check("epoll add pipe", epoll_ctl(ep, EPOLL_CTL_ADD, pipefd[0], &event) == 0);
    check("epoll add dup EEXIST",
          epoll_ctl(ep, EPOLL_CTL_ADD, pipefd[0], &event) < 0 && errno == EEXIST);
    event.data.fd = ev;
    check("epoll add eventfd", epoll_ctl(ep, EPOLL_CTL_ADD, ev, &event) == 0);

    check("epoll wait timeout", epoll_wait(ep, out, 8, 30) == 0);

    write(pipefd[1], "x", 1);
    int n = epoll_wait(ep, out, 8, 100);
    check("epoll pipe ready", n == 1 && out[0].data.fd == pipefd[0] &&
                                  (out[0].events & EPOLLIN));
    n = epoll_wait(ep, out, 8, 0);
    check("epoll level triggered", n == 1);
    char byte;
    read(pipefd[0], &byte, 1);
    check("epoll drained", epoll_wait(ep, out, 8, 0) == 0);

    uint64_t one = 1;
    write(ev, &one, sizeof(one));
    write(pipefd[1], "y", 1);
    n = epoll_wait(ep, out, 8, 100);
    check("epoll two ready", n == 2);
    read(pipefd[0], &byte, 1);
    read(ev, &one, sizeof(one));

    event.events = EPOLLIN | EPOLLET;
    event.data.fd = pipefd[0];
    check("epoll mod edge", epoll_ctl(ep, EPOLL_CTL_MOD, pipefd[0], &event) == 0);
    write(pipefd[1], "ab", 2);
    check("epoll edge first", epoll_wait(ep, out, 8, 100) == 1);
    check("epoll edge silent", epoll_wait(ep, out, 8, 20) == 0);
    read(pipefd[0], &byte, 1);
    read(pipefd[0], &byte, 1);
    write(pipefd[1], "c", 1);
    check("epoll edge again", epoll_wait(ep, out, 8, 100) == 1);
    read(pipefd[0], &byte, 1);

    event.events = EPOLLIN | EPOLLONESHOT;
    epoll_ctl(ep, EPOLL_CTL_MOD, pipefd[0], &event);
    write(pipefd[1], "d", 1);
    check("epoll oneshot first", epoll_wait(ep, out, 8, 100) == 1);
    check("epoll oneshot disarmed", epoll_wait(ep, out, 8, 20) == 0);
    epoll_ctl(ep, EPOLL_CTL_MOD, pipefd[0], &event);
    check("epoll oneshot rearm", epoll_wait(ep, out, 8, 100) == 1);
    read(pipefd[0], &byte, 1);

    event.events = EPOLLIN;
    epoll_ctl(ep, EPOLL_CTL_MOD, pipefd[0], &event);
    close(pipefd[1]);
    n = epoll_wait(ep, out, 8, 100);
    check("epoll HUP", n == 1 && (out[0].events & EPOLLHUP));
    check("epoll del", epoll_ctl(ep, EPOLL_CTL_DEL, pipefd[0], 0) == 0);
    check("epoll del missing",
          epoll_ctl(ep, EPOLL_CTL_DEL, pipefd[0], 0) < 0 && errno == ENOENT);
    check("epoll add self", epoll_ctl(ep, EPOLL_CTL_ADD, ep, &event) < 0);

    int ep2 = epoll_create1(0);
    event.events = EPOLLIN;
    event.data.fd = ep;
    check("epoll nested add", epoll_ctl(ep2, EPOLL_CTL_ADD, ep, &event) == 0);
    write(ev, &one, sizeof(one));
    check("epoll nested ready", epoll_wait(ep2, out, 8, 100) == 1);
    read(ev, &one, sizeof(one));

    event.events = EPOLLIN | EPOLLOUT;
    event.data.fd = ev;
    epoll_ctl(ep, EPOLL_CTL_MOD, ev, &event);
    n = epoll_wait(ep, out, 8, 0);
    check("epoll eventfd writable", n == 1 && (out[0].events & EPOLLOUT));

    close(ev);
    n = epoll_wait(ep, out, 8, 20);
    check("epoll closed fd dropped", n == 0);
    close(pipefd[0]);
    close(ep2);
    close(ep);
}

static void test_epoll_wakeup_across_fork(void)
{
    int ev = eventfd(0, 0);
    int ep = epoll_create1(0);
    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = ev;
    epoll_ctl(ep, EPOLL_CTL_ADD, ev, &event);
    pid_t child = fork();
    if (child == 0)
    {
        struct timespec nap = {0, 60 * 1000000L};
        nanosleep(&nap, 0);
        uint64_t one = 1;
        write(ev, &one, sizeof(one));
        _exit(0);
    }
    struct epoll_event out[2];
    long start = now_ms();
    int n = epoll_wait(ep, out, 2, 2000);
    long elapsed = now_ms() - start;
    check("epoll wake from child", n == 1 && elapsed >= 40 && elapsed < 1000);
    int status = 0;
    waitpid(child, &status, 0);
    close(ep);
    close(ev);
}

int main(void)
{
    test_eventfd();
    test_timerfd();
    test_epoll();
    test_epoll_wakeup_across_fork();
    printf("posixtest done failures=%d\n", failures);
    return failures ? 1 : 0;
}
