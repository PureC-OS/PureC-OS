// PureC hosted libc: signal stubs (see hosted/signal.h). Delivery does
// not exist; registration calls fail with ENOSYS so -run-only paths in
// ported programs degrade gracefully instead of silently arming nothing.

#include "include/hosted/signal.h"
#include "include/hosted/sys/ucontext.h"
#include "include/hosted/errno.h"
#include <stddef.h>

int signal(int signum, sighandler_t handler) {
    (void)signum;
    (void)handler;
    errno = ENOSYS;
    return -1;
}

int sigaction(int signum, const struct sigaction *action,
              struct sigaction *previous) {
    (void)signum;
    (void)action;
    (void)previous;
    errno = ENOSYS;
    return -1;
}

int sigemptyset(sigset_t *set) {
    if (!set) {
        errno = EINVAL;
        return -1;
    }
    *set = 0;
    return 0;
}

int sigaddset(sigset_t *set, int signum) {
    if (!set || signum < 1 || signum > 64) {
        errno = EINVAL;
        return -1;
    }
    *set |= 1UL << (signum - 1);
    return 0;
}

int sigdelset(sigset_t *set, int signum) {
    if (!set || signum < 1 || signum > 64) {
        errno = EINVAL;
        return -1;
    }
    *set &= ~(1UL << (signum - 1));
    return 0;
}

int sigismember(const sigset_t *set, int signum) {
    if (!set || signum < 1 || signum > 64) {
        errno = EINVAL;
        return -1;
    }
    return (*set & (1UL << (signum - 1))) ? 1 : 0;
}

int sigprocmask(int how, const sigset_t *set, sigset_t *previous) {
    (void)how;
    (void)set;
    if (previous) *previous = 0;
    return 0; // single-threaded, no mask to manage
}

int raise(int signum) {
    (void)signum;
    errno = ENOSYS;
    return -1;
}

int kill(int pid, int signum) {
    (void)pid;
    (void)signum;
    errno = ENOSYS;
    return -1;
}

int getcontext(ucontext_t *context) {
    (void)context;
    errno = ENOSYS;
    return -1;
}
