#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <grp.h>
#include <pthread.h>
#include <pwd.h>
#include <sched.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/file.h>
#include <sys/prctl.h>
#include <sys/random.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/uio.h>
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
    long start = now_ms();
    int armed = timerfd_settime(fd, 0, &spec, 0);
    int early = read(fd, &expirations, sizeof(expirations)) < 0 && errno == EAGAIN;
    struct pollfd pfd = {fd, POLLIN, 0};
    int ready = poll(&pfd, 1, 1000);
    long elapsed = now_ms() - start;
    check("timerfd settime", armed == 0);
    check("timerfd not yet", early);
    check("timerfd poll fires", ready == 1 && (pfd.revents & POLLIN));
    check("timerfd delay", elapsed >= 40 && elapsed < 400);
    if (!(elapsed >= 40 && elapsed < 400))
        printf("posixtest   timerfd delay elapsed=%ldms\n", elapsed);
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
    if (!(elapsed >= 20 && elapsed < 400))
        printf("posixtest   timerfd blocking elapsed=%ldms\n", elapsed);
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


static int send_fds(int sock, const int *fds, int count, char marker)
{
    char control[CMSG_SPACE(sizeof(int) * 8)];
    memset(control, 0, sizeof(control));
    struct iovec iov = {&marker, 1};
    struct msghdr msg;
    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control;
    msg.msg_controllen = CMSG_SPACE(sizeof(int) * count);
    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int) * count);
    memcpy(CMSG_DATA(cmsg), fds, sizeof(int) * count);
    return (int)sendmsg(sock, &msg, 0);
}

static int recv_fds(int sock, int *fds, int capacity, char *marker, int *flags)
{
    char control[CMSG_SPACE(sizeof(int) * 8)];
    memset(control, 0, sizeof(control));
    struct iovec iov = {marker, 1};
    struct msghdr msg;
    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control;
    msg.msg_controllen = CMSG_LEN(sizeof(int) * capacity);
    ssize_t got = recvmsg(sock, &msg, 0);
    if (flags)
        *flags = msg.msg_flags;
    if (got < 0)
        return -1;
    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    if (!cmsg || cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS)
        return 0;
    int count = (int)((cmsg->cmsg_len - CMSG_LEN(0)) / sizeof(int));
    memcpy(fds, CMSG_DATA(cmsg), sizeof(int) * count);
    return count;
}

static void test_socketpair(void)
{
    int sv[2];
    check("socketpair create", socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    char buffer[16];
    check("socketpair a->b", write(sv[0], "hello", 5) == 5 &&
                                 read(sv[1], buffer, sizeof(buffer)) == 5 &&
                                 memcmp(buffer, "hello", 5) == 0);
    check("socketpair b->a", write(sv[1], "back", 4) == 4 &&
                                 read(sv[0], buffer, sizeof(buffer)) == 4 &&
                                 memcmp(buffer, "back", 4) == 0);
    int flags = fcntl(sv[0], F_GETFL);
    fcntl(sv[0], F_SETFL, flags | O_NONBLOCK);
    check("socketpair EAGAIN",
          read(sv[0], buffer, sizeof(buffer)) < 0 && errno == EAGAIN);
    struct pollfd pfd = {sv[0], POLLIN | POLLOUT, 0};
    poll(&pfd, 1, 0);
    check("socketpair poll OUT only", pfd.revents == POLLOUT);
    write(sv[1], "x", 1);
    poll(&pfd, 1, 0);
    check("socketpair poll IN", (pfd.revents & POLLIN) != 0);
    read(sv[0], buffer, 1);
    check("socketpair peek", write(sv[1], "peek", 4) == 4 &&
                                 recv(sv[0], buffer, 4, MSG_PEEK) == 4 &&
                                 recv(sv[0], buffer, 4, 0) == 4);
    check("socketpair SO_TYPE", ({
              int type = 0;
              socklen_t length = sizeof(type);
              getsockopt(sv[0], SOL_SOCKET, SO_TYPE, &type, &length) == 0 &&
                  type == SOCK_STREAM;
          }));
    write(sv[1], "tail", 4);
    shutdown(sv[1], SHUT_WR);
    check("socketpair data before EOF", read(sv[0], buffer, 16) == 4);
    check("socketpair shutdown EOF", read(sv[0], buffer, 16) == 0);
    check("socketpair other dir alive", write(sv[0], "z", 1) == 1);
    close(sv[1]);
    check("socketpair peer close EOF", ({
              read(sv[0], buffer, 1);
              read(sv[0], buffer, 1) == 0;
          }));
    check("socketpair EPIPE", send(sv[0], "q", 1, MSG_NOSIGNAL) < 0 && errno == EPIPE);
    pfd.events = POLLIN;
    poll(&pfd, 1, 0);
    check("socketpair HUP", (pfd.revents & POLLHUP) != 0);
    close(sv[0]);

    socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    pid_t child = fork();
    if (child == 0)
    {
        close(sv[0]);
        char got[8];
        ssize_t n = read(sv[1], got, sizeof(got));
        for (ssize_t i = 0; i < n; i++)
            got[i] = (char)(got[i] + 1);
        write(sv[1], got, (size_t)n);
        _exit(0);
    }
    close(sv[1]);
    write(sv[0], "abc", 3);
    ssize_t n = read(sv[0], buffer, sizeof(buffer));
    check("socketpair across fork", n == 3 && memcmp(buffer, "bcd", 3) == 0);
    int status;
    waitpid(child, &status, 0);
    close(sv[0]);

    int big[2];
    socketpair(AF_UNIX, SOCK_STREAM, 0, big);
    static char chunk[8192];
    memset(chunk, 'k', sizeof(chunk));
    child = fork();
    if (child == 0)
    {
        close(big[0]);
        for (int i = 0; i < 16; i++)
            write(big[1], chunk, sizeof(chunk));
        _exit(0);
    }
    close(big[1]);
    long total = 0;
    ssize_t got;
    static char sink[4096];
    while ((got = read(big[0], sink, sizeof(sink))) > 0)
        total += got;
    check("socketpair 128K blocking", total == 16 * 8192);
    waitpid(child, &status, 0);
    close(big[0]);
}

static void test_unix_named(void)
{
    struct sockaddr_un address;
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    strcpy(address.sun_path, "/tmp/posixtest.sock");
    unlink(address.sun_path);

    int server = socket(AF_UNIX, SOCK_STREAM, 0);
    check("unix socket create", server >= 0);
    check("unix bind path", bind(server, (struct sockaddr *)&address,
                                 sizeof(address)) == 0);
    int dup = socket(AF_UNIX, SOCK_STREAM, 0);
    check("unix bind EADDRINUSE", bind(dup, (struct sockaddr *)&address,
                                       sizeof(address)) < 0 &&
                                      errno == EADDRINUSE);
    close(dup);
    check("unix listen", listen(server, 4) == 0);
    int flags = fcntl(server, F_GETFL);
    fcntl(server, F_SETFL, flags | O_NONBLOCK);
    check("unix accept EAGAIN", accept(server, 0, 0) < 0 && errno == EAGAIN);
    fcntl(server, F_SETFL, flags);

    int client = socket(AF_UNIX, SOCK_STREAM, 0);
    check("unix connect", connect(client, (struct sockaddr *)&address,
                                  sizeof(address)) == 0);
    struct pollfd pfd = {server, POLLIN, 0};
    check("unix listener POLLIN", poll(&pfd, 1, 100) == 1 && (pfd.revents & POLLIN));
    struct sockaddr_un peer;
    socklen_t peer_length = sizeof(peer);
    int accepted = accept(server, (struct sockaddr *)&peer, &peer_length);
    check("unix accept", accepted >= 0);
    char buffer[32];
    check("unix client->server", write(client, "ping", 4) == 4 &&
                                     read(accepted, buffer, sizeof(buffer)) == 4 &&
                                     memcmp(buffer, "ping", 4) == 0);
    check("unix server->client", write(accepted, "pong", 4) == 4 &&
                                     read(client, buffer, sizeof(buffer)) == 4 &&
                                     memcmp(buffer, "pong", 4) == 0);

    struct sockaddr_un name;
    socklen_t name_length = sizeof(name);
    memset(&name, 0, sizeof(name));
    check("unix getsockname", getsockname(server, (struct sockaddr *)&name,
                                          &name_length) == 0 &&
                                  name.sun_family == AF_UNIX &&
                                  strcmp(name.sun_path, "/tmp/posixtest.sock") == 0);
    name_length = sizeof(name);
    check("unix getpeername", getpeername(client, (struct sockaddr *)&name,
                                          &name_length) == 0 &&
                                  strcmp(name.sun_path, "/tmp/posixtest.sock") == 0);
    struct ucred cred;
    socklen_t cred_length = sizeof(cred);
    check("unix SO_PEERCRED", getsockopt(accepted, SOL_SOCKET, SO_PEERCRED, &cred,
                                         &cred_length) == 0 &&
                                  cred.pid == (int)getpid());

    close(client);
    check("unix peer close EOF", read(accepted, buffer, sizeof(buffer)) == 0);
    close(accepted);
    close(server);
    unlink(address.sun_path);

    int orphan = socket(AF_UNIX, SOCK_STREAM, 0);
    check("unix connect ENOENT", connect(orphan, (struct sockaddr *)&address,
                                         sizeof(address)) < 0 &&
                                     (errno == ENOENT || errno == ECONNREFUSED));
    close(orphan);

    struct sockaddr_un abstract_address;
    memset(&abstract_address, 0, sizeof(abstract_address));
    abstract_address.sun_family = AF_UNIX;
    memcpy(abstract_address.sun_path + 1, "pc-abstract", 11);
    socklen_t abstract_length = (socklen_t)(2 + 1 + 11);
    int abstract_server = socket(AF_UNIX, SOCK_STREAM, 0);
    check("unix abstract bind", bind(abstract_server, (struct sockaddr *)&abstract_address,
                                     abstract_length) == 0 &&
                                    listen(abstract_server, 1) == 0);
    pid_t child = fork();
    if (child == 0)
    {
        int c = socket(AF_UNIX, SOCK_STREAM, 0);
        if (connect(c, (struct sockaddr *)&abstract_address, abstract_length) != 0)
            _exit(2);
        write(c, "from-child", 10);
        _exit(0);
    }
    int connection = accept(abstract_server, 0, 0);
    ssize_t got = connection >= 0 ? read(connection, buffer, sizeof(buffer)) : -1;
    check("unix abstract across fork", got == 10 && memcmp(buffer, "from-child", 10) == 0);
    int status;
    waitpid(child, &status, 0);
    close(connection);
    close(abstract_server);

    int autobound = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un auto_address;
    memset(&auto_address, 0, sizeof(auto_address));
    auto_address.sun_family = AF_UNIX;
    check("unix autobind", bind(autobound, (struct sockaddr *)&auto_address,
                                sizeof(sa_family_t)) == 0);
    close(autobound);
}

static void test_scm_rights(void)
{
    int sv[2];
    socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    int p[2];
    pipe(p);
    int pass[1] = {p[0]};
    check("scm send one fd", send_fds(sv[0], pass, 1, 'A') == 1);
    int received[8];
    char marker = 0;
    int mflags = 0;
    int count = recv_fds(sv[1], received, 8, &marker, &mflags);
    check("scm recv one fd", count == 1 && marker == 'A' && received[0] != p[0]);
    write(p[1], "through", 7);
    char buffer[16];
    check("scm fd works", count == 1 && read(received[0], buffer, sizeof(buffer)) == 7 &&
                              memcmp(buffer, "through", 7) == 0);
    close(received[0]);

    int ev = eventfd(7, 0);
    int u2[2];
    socketpair(AF_UNIX, SOCK_STREAM, 0, u2);
    int triple[3] = {ev, u2[0], p[1]};
    check("scm send three", send_fds(sv[0], triple, 3, 'B') == 1);
    count = recv_fds(sv[1], received, 8, &marker, &mflags);
    check("scm recv three", count == 3 && marker == 'B');
    uint64_t value = 0;
    check("scm eventfd passed", count == 3 && read(received[0], &value, 8) == 8 && value == 7);
    check("scm socket passed", count == 3 && write(received[1], "S", 1) == 1 &&
                                   read(u2[1], buffer, 1) == 1 && buffer[0] == 'S');
    check("scm pipe write end passed", count == 3 && write(received[2], "W", 1) == 1);
    for (int i = 0; i < count; i++)
        close(received[i]);

    send_fds(sv[0], triple, 3, 'C');
    count = recv_fds(sv[1], received, 1, &marker, &mflags);
    check("scm CTRUNC", count == 1 && (mflags & MSG_CTRUNC));
    if (count == 1)
        close(received[0]);

    write(sv[0], "plain", 5);
    count = recv_fds(sv[1], received, 8, &marker, &mflags);
    check("scm no fds on plain", count == 0 && marker == 'p');
    read(sv[1], buffer, 4);

    int dup_sent = dup(p[1]);
    send_fds(sv[0], &dup_sent, 1, 'D');
    close(dup_sent);
    close(p[1]);
    count = recv_fds(sv[1], received, 8, &marker, &mflags);
    check("scm survives sender close", count == 1 && write(received[0], "Z", 1) == 1);
    if (count == 1)
        close(received[0]);

    send_fds(sv[0], triple, 3, 'E');
    close(sv[1]);
    close(sv[0]);
    close(ev);
    close(u2[0]);
    close(u2[1]);
    close(p[0]);
}

static void test_shared_memory(void)
{
    volatile char *shared = mmap(0, 8192, PROT_READ | PROT_WRITE,
                                 MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    check("shm anon mmap", shared != MAP_FAILED);
    if (shared == MAP_FAILED)
        return;
    shared[0] = 'p';
    shared[4096] = 'q';
    int to_child[2], to_parent[2];
    pipe(to_child);
    pipe(to_parent);
    pid_t child = fork();
    if (child == 0)
    {
        char token;
        int ok = shared[0] == 'p' && shared[4096] == 'q';
        shared[1] = 'c';
        shared[4097] = ok ? 'k' : 'x';
        write(to_parent[1], "1", 1);
        read(to_child[0], &token, 1);
        ok = ok && shared[2] == 'P';
        shared[3] = ok ? 'Y' : 'N';
        _exit(0);
    }
    char token;
    read(to_parent[0], &token, 1);
    check("shm anon child write visible", shared[1] == 'c' && shared[4097] == 'k');
    shared[2] = 'P';
    write(to_child[1], "1", 1);
    int status = 0;
    waitpid(child, &status, 0);
    check("shm anon parent write visible", shared[3] == 'Y');

    volatile char *private_map = mmap(0, 4096, PROT_READ | PROT_WRITE,
                                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    private_map[0] = 'a';
    child = fork();
    if (child == 0)
    {
        private_map[0] = 'z';
        _exit(0);
    }
    waitpid(child, &status, 0);
    check("shm private stays COW", private_map[0] == 'a');

    check("shm munmap shared", munmap((void *)shared, 8192) == 0);
    munmap((void *)private_map, 4096);

    int fd = memfd_create("posixtest", MFD_CLOEXEC);
    check("memfd create", fd >= 0);
    check("memfd ftruncate", ftruncate(fd, 8192) == 0);
    struct stat info;
    check("memfd fstat size", fstat(fd, &info) == 0 && info.st_size == 8192);
    char *first = mmap(0, 8192, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    char *second = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 4096);
    check("memfd mmap", first != MAP_FAILED && second != MAP_FAILED);
    if (first != MAP_FAILED && second != MAP_FAILED)
    {
        memcpy(first + 4096, "second-page", 12);
        check("memfd offset maps same page", memcmp(second, "second-page", 12) == 0);
        second[100] = 'S';
        check("memfd aliasing write", first[4096 + 100] == 'S');
        close(fd);
        first[0] = 'f';
        check("memfd survives close", first[0] == 'f' && memcmp(second, "second-page", 12) == 0);
        munmap(first, 8192);
        check("memfd partial unmap keeps rest", second[100] == 'S');
        munmap(second, 4096);
    }

    int sv[2];
    socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    fd = memfd_create("wl_shm", MFD_CLOEXEC);
    ftruncate(fd, 4096);
    char *buffer = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    memcpy(buffer, "pixels", 7);
    child = fork();
    if (child == 0)
    {
        close(sv[0]);
        int received[2];
        char marker;
        int mflags;
        int n = recv_fds(sv[1], received, 2, &marker, &mflags);
        int ok = n == 1;
        if (ok)
        {
            char *view = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, received[0], 0);
            ok = view != MAP_FAILED && memcmp(view, "pixels", 7) == 0;
            if (ok)
                memcpy(view + 16, "drawn", 6);
        }
        write(sv[1], ok ? "K" : "X", 1);
        _exit(0);
    }
    close(sv[1]);
    int pass[1] = {fd};
    send_fds(sv[0], pass, 1, 'M');
    char verdict = 0;
    read(sv[0], &verdict, 1);
    waitpid(child, &status, 0);
    check("memfd passed over SCM_RIGHTS", verdict == 'K' && memcmp(buffer + 16, "drawn", 6) == 0);
    munmap(buffer, 4096);
    close(fd);
    close(sv[0]);

    shm_unlink("/posixtest-seg");
    int named = shm_open("/posixtest-seg", O_CREAT | O_EXCL | O_RDWR, 0600);
    check("shm_open create", named >= 0);
    check("shm_open EEXIST", shm_open("/posixtest-seg", O_CREAT | O_EXCL | O_RDWR, 0600) < 0 &&
                                 errno == EEXIST);
    ftruncate(named, 4096);
    char *segment = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, named, 0);
    memcpy(segment, "named", 6);
    int again = shm_open("/posixtest-seg", O_RDWR, 0);
    char *view = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, again, 0);
    check("shm_open second opener sees data", view != MAP_FAILED && memcmp(view, "named", 6) == 0);
    check("shm_unlink", shm_unlink("/posixtest-seg") == 0);
    check("shm_open after unlink ENOENT",
          shm_open("/posixtest-seg", O_RDWR, 0) < 0 && errno == ENOENT);
    check("shm mapping survives unlink", memcmp(segment, "named", 6) == 0);
    munmap(segment, 4096);
    munmap(view, 4096);
    close(named);
    close(again);

    char *anon = mmap(0, 8192, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    memset(anon, 0x5A, 8192);
    check("madvise DONTNEED", madvise(anon, 4096, MADV_DONTNEED) == 0);
    check("madvise zeroes private page", anon[0] == 0 && anon[4095] == 0 && anon[4096] == 0x5A);
    check("madvise unaligned EINVAL", madvise(anon + 1, 4096, MADV_DONTNEED) < 0 && errno == EINVAL);
    munmap(anon, 8192);

    char *keep = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    keep[10] = 'K';
    madvise(keep, 4096, MADV_DONTNEED);
    check("madvise keeps shared data", keep[10] == 'K');
    munmap(keep, 4096);
}

static volatile char *alt_probe_low;
static volatile char *alt_probe_high;
static volatile int handler_on_alt;
static volatile int handler_flag_seen;
static volatile int handler_nested_ok;
static volatile int handler_ran;

static void usr1_alt_handler(int sig)
{
    char marker;
    (void)sig;
    handler_ran++;
    handler_on_alt = (&marker >= alt_probe_low && &marker < alt_probe_high);
    stack_t current;
    sigaltstack(0, &current);
    handler_flag_seen = current.ss_flags;
    stack_t other;
    other.ss_sp = (void *)alt_probe_low;
    other.ss_size = 16384;
    other.ss_flags = 0;
    handler_nested_ok = sigaltstack(&other, 0) < 0 && errno == EPERM;
}

static void usr1_plain_handler(int sig)
{
    char marker;
    (void)sig;
    handler_ran++;
    handler_on_alt = (&marker >= alt_probe_low && &marker < alt_probe_high);
}

static void overflow_handler(int sig)
{
    (void)sig;
    _exit(42);
}

static int recurse_forever(volatile char *seed, int depth)
{
    volatile char pad[512];
    pad[0] = (char)depth;
    pad[511] = seed[0];
    return recurse_forever(pad, depth + 1) + pad[0];
}

static void test_sigaltstack(void)
{
    size_t size = 32768;
    char *stack = mmap(0, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    alt_probe_low = stack;
    alt_probe_high = stack + size;
    stack_t request;
    request.ss_sp = stack;
    request.ss_size = size;
    request.ss_flags = 0;
    check("sigaltstack set", sigaltstack(&request, 0) == 0);
    stack_t readback;
    memset(&readback, 0, sizeof(readback));
    check("sigaltstack get", sigaltstack(0, &readback) == 0 && readback.ss_sp == stack &&
                                 readback.ss_size == size && !(readback.ss_flags & SS_DISABLE));
    request.ss_size = 512;
    check("sigaltstack too small", sigaltstack(&request, 0) < 0 && errno == ENOMEM);
    request.ss_size = size;

    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = usr1_alt_handler;
    action.sa_flags = SA_ONSTACK;
    sigaction(SIGUSR1, &action, 0);
    handler_ran = 0;
    kill(getpid(), SIGUSR1);
    check("sigaltstack handler ran", handler_ran == 1);
    check("sigaltstack handler on alt stack", handler_on_alt == 1);
    check("sigaltstack SS_ONSTACK reported", (handler_flag_seen & SS_ONSTACK) != 0);
    check("sigaltstack EPERM while on stack", handler_nested_ok == 1);

    action.sa_handler = usr1_plain_handler;
    action.sa_flags = 0;
    sigaction(SIGUSR1, &action, 0);
    handler_ran = 0;
    handler_on_alt = -1;
    kill(getpid(), SIGUSR1);
    check("sigaltstack ignored without SA_ONSTACK", handler_ran == 1 && handler_on_alt == 0);

    memset(&readback, 0, sizeof(readback));
    sigaltstack(0, &readback);
    check("sigaltstack not on stack outside handler", !(readback.ss_flags & SS_ONSTACK));
    request.ss_flags = SS_DISABLE;
    check("sigaltstack disable", sigaltstack(&request, 0) == 0);
    sigaltstack(0, &readback);
    check("sigaltstack disabled state", (readback.ss_flags & SS_DISABLE) != 0);
    signal(SIGUSR1, SIG_DFL);

    pid_t child = fork();
    if (child == 0)
    {
        stack_t child_stack;
        child_stack.ss_sp = stack;
        child_stack.ss_size = size;
        child_stack.ss_flags = 0;
        sigaltstack(&child_stack, 0);
        struct sigaction segv;
        memset(&segv, 0, sizeof(segv));
        segv.sa_handler = overflow_handler;
        segv.sa_flags = SA_ONSTACK;
        sigaction(SIGSEGV, &segv, 0);
        volatile char seed[1] = {1};
        recurse_forever(seed, 0);
        _exit(1);
    }
    int status = 0;
    waitpid(child, &status, 0);
    check("sigaltstack catches stack overflow", WIFEXITED(status) && WEXITSTATUS(status) == 42);
    munmap(stack, size);
}

static void test_realpath(void)
{
    char resolved[512];
    check("realpath dot segments",
          realpath("/usr/./bin/../bin/posixtest", resolved) &&
              strcmp(resolved, "/usr/bin/posixtest") == 0);
    char saved[512];
    getcwd(saved, sizeof(saved));
    chdir("/usr/bin");
    check("realpath ./relative",
          realpath("./posixtest", resolved) && strcmp(resolved, "/usr/bin/posixtest") == 0);
    check("realpath ../relative",
          realpath("../bin/posixtest", resolved) && strcmp(resolved, "/usr/bin/posixtest") == 0);
    check("access ./relative", access("./posixtest", F_OK) == 0);
    chdir(saved);
}

static int atfork_prepare_runs;
static int atfork_parent_runs;
static int atfork_child_runs;
static pthread_barrier_t test_barrier;
static pthread_spinlock_t test_spin;
static volatile int barrier_serial;
static volatile int barrier_passed;
static volatile long spin_counter;

static void atfork_prepare(void) { atfork_prepare_runs++; }
static void atfork_parent(void) { atfork_parent_runs++; }
static void atfork_child(void) { atfork_child_runs++; }

static void *barrier_worker(void *argument)
{
    (void)argument;
    for (int i = 0; i < 1000; i++)
    {
        pthread_spin_lock(&test_spin);
        spin_counter++;
        pthread_spin_unlock(&test_spin);
    }
    int result = pthread_barrier_wait(&test_barrier);
    if (result == PTHREAD_BARRIER_SERIAL_THREAD)
        __atomic_add_fetch(&barrier_serial, 1, __ATOMIC_SEQ_CST);
    __atomic_add_fetch(&barrier_passed, 1, __ATOMIC_SEQ_CST);
    return 0;
}

static void test_libc_extra(void)
{
    char buffer[256];
    struct passwd entry;
    struct passwd *found = 0;
    check("getpwuid_r root", getpwuid_r(0, &entry, buffer, sizeof(buffer), &found) == 0 &&
                                 found && strcmp(found->pw_name, "root") == 0);
    check("getpwuid_r ERANGE", getpwuid_r(0, &entry, buffer, 4, &found) == ERANGE);
    check("getpwnam_r unknown", getpwnam_r("no-such-user-xyz", &entry, buffer, sizeof(buffer), &found) == 0 &&
                                    found == 0);
    struct group group;
    struct group *group_found = 0;
    int group_status = getgrgid_r(0, &group, buffer, sizeof(buffer), &group_found);
    check("getgrgid_r gid 0", group_status == 0 && group_found != 0);

    unsigned char first[32], second[32];
    memset(first, 0, sizeof(first));
    memset(second, 0, sizeof(second));
    check("getrandom length", getrandom(first, sizeof(first), 0) == (ssize_t)sizeof(first));
    getentropy(second, sizeof(second));
    check("getrandom differs", memcmp(first, second, sizeof(first)) != 0);

    struct timespec request = {0, 30 * 1000000L};
    long start = now_ms();
    check("clock_nanosleep relative", clock_nanosleep(CLOCK_MONOTONIC, 0, &request, 0) == 0 &&
                                          now_ms() - start >= 25);
    struct timespec absolute;
    clock_gettime(CLOCK_MONOTONIC, &absolute);
    absolute.tv_nsec += 30 * 1000000L;
    if (absolute.tv_nsec >= 1000000000L)
    {
        absolute.tv_sec++;
        absolute.tv_nsec -= 1000000000L;
    }
    start = now_ms();
    check("clock_nanosleep TIMER_ABSTIME", clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &absolute, 0) == 0 &&
                                               now_ms() - start >= 20);

    int pipefd[2];
    pipe(pipefd);
    struct pollfd pfd = {pipefd[0], POLLIN, 0};
    struct timespec poll_timeout = {0, 40 * 1000000L};
    start = now_ms();
    int ready = ppoll(&pfd, 1, &poll_timeout, 0);
    check("ppoll timeout", ready == 0 && now_ms() - start >= 30);
    write(pipefd[1], "x", 1);
    check("ppoll ready", ppoll(&pfd, 1, &poll_timeout, 0) == 1);
    close(pipefd[0]);
    close(pipefd[1]);

    pthread_atfork(atfork_prepare, atfork_parent, atfork_child);
    int fork_pipe[2];
    pipe(fork_pipe);
    pid_t child = fork();
    if (child == 0)
    {
        char ok = (atfork_prepare_runs == 1 && atfork_child_runs == 1 && atfork_parent_runs == 0) ? 'Y' : 'N';
        write(fork_pipe[1], &ok, 1);
        _exit(0);
    }
    char verdict = 0;
    read(fork_pipe[0], &verdict, 1);
    int status = 0;
    waitpid(child, &status, 0);
    check("pthread_atfork child ran", verdict == 'Y');
    check("pthread_atfork parent ran", atfork_prepare_runs == 1 && atfork_parent_runs == 1 &&
                                           atfork_child_runs == 0);
    close(fork_pipe[0]);
    close(fork_pipe[1]);

    pthread_spin_init(&test_spin, 0);
    pthread_barrier_init(&test_barrier, 0, 4);
    pthread_t threads[3];
    for (int i = 0; i < 3; i++)
        pthread_create(&threads[i], 0, barrier_worker, 0);
    barrier_worker(0);
    for (int i = 0; i < 3; i++)
        pthread_join(threads[i], 0);
    check("pthread_barrier all passed", barrier_passed == 4);
    check("pthread_barrier one serial", barrier_serial == 1);
    check("pthread_spin mutual exclusion", spin_counter == 4000);
    pthread_barrier_destroy(&test_barrier);

    char name[32];
    check("prctl PR_SET_NAME", prctl(PR_SET_NAME, "ptest-name") == 0);
    memset(name, 0, sizeof(name));
    check("prctl PR_GET_NAME", prctl(PR_GET_NAME, name) == 0 && strcmp(name, "ptest-name") == 0);

    cpu_set_t cpus;
    CPU_ZERO(&cpus);
    check("sched_getaffinity", sched_getaffinity(0, sizeof(cpus), &cpus) == 0 && CPU_ISSET(0, &cpus));

    int fd = open("/tmp/posixtest.fa", O_CREAT | O_RDWR | O_TRUNC, 0600);
    struct stat info;
    check("posix_fallocate grows", fd >= 0 && posix_fallocate(fd, 0, 8192) == 0 &&
                                       fstat(fd, &info) == 0 && info.st_size == 8192);
    check("flock lock+unlock", flock(fd, LOCK_EX | LOCK_NB) == 0 && flock(fd, LOCK_UN) == 0);
    check("flock bad fd", flock(9999, LOCK_EX) < 0 && errno == EBADF);
    close(fd);
    unlink("/tmp/posixtest.fa");
}

int main(void)
{
    test_realpath();
    test_libc_extra();
    test_eventfd();
    test_timerfd();
    test_epoll();
    test_epoll_wakeup_across_fork();
    test_socketpair();
    test_unix_named();
    test_scm_rights();
    test_shared_memory();
    test_sigaltstack();
    printf("posixtest done failures=%d\n", failures);
    return failures ? 1 : 0;
}
