// PureC hosted libc: signals on top of the kernel sigaction/sigprocmask/kill
// syscalls (delivery on return to user mode).

#include "include/hosted/signal.h"
#include "include/hosted/sys/ucontext.h"
#include "include/hosted/errno.h"
#include "include/purec.h"
#include <stddef.h>
#include <stdint.h>

extern void __purec_sigreturn(void);

__asm__(
    ".text\n"
    ".global __purec_sigreturn\n"
    "__purec_sigreturn:\n"
    "    movl $338, %eax\n"
    "    int $0x80\n"
    "    hlt\n");

int sigaction(int signum, const struct sigaction *action,
              struct sigaction *previous) {
    struct ksigaction incoming;
    struct ksigaction outgoing;
    if (action) {
        incoming.handler = (action->sa_flags & SA_SIGINFO)
                               ? (uint64_t)(uintptr_t)action->sa_sigaction
                               : (uint64_t)(uintptr_t)action->sa_handler;
        incoming.mask = action->sa_mask;
        incoming.restorer = (uint64_t)(uintptr_t)__purec_sigreturn;
        incoming.flags = (uint32_t)action->sa_flags;
        incoming.reserved = 0;
    }
    int64_t result = pc_syscall(SYS_SIGACTION, (uint64_t)(uint32_t)signum,
                                action ? (uint64_t)(uintptr_t)&incoming : 0,
                                previous ? (uint64_t)(uintptr_t)&outgoing : 0);
    if (result < 0) {
        errno = (int)-result;
        return -1;
    }
    if (previous) {
        previous->sa_handler = (sighandler_t)(uintptr_t)outgoing.handler;
        previous->sa_sigaction = (void (*)(int, siginfo_t *, void *))(uintptr_t)outgoing.handler;
        previous->sa_mask = outgoing.mask;
        previous->sa_flags = (int)outgoing.flags;
    }
    return 0;
}

sighandler_t signal(int signum, sighandler_t handler) {
    struct sigaction action;
    struct sigaction previous;
    action.sa_handler = handler;
    action.sa_sigaction = 0;
    action.sa_mask = 0;
    action.sa_flags = SA_RESTART;
    if (sigaction(signum, &action, &previous) < 0) return SIG_ERR;
    return previous.sa_handler;
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
    uint64_t incoming = set ? (uint64_t)*set : 0;
    uint64_t outgoing = 0;
    int64_t result = pc_syscall(SYS_SIGPROCMASK, (uint64_t)(uint32_t)how,
                                set ? (uint64_t)(uintptr_t)&incoming : 0,
                                previous ? (uint64_t)(uintptr_t)&outgoing : 0);
    if (result < 0) {
        errno = (int)-result;
        return -1;
    }
    if (previous) *previous = (sigset_t)outgoing;
    return 0;
}

int sigpending(sigset_t *set) {
    uint64_t pending = 0;
    int64_t result = pc_syscall(SYS_SIGPENDING, (uint64_t)(uintptr_t)&pending, 0, 0);
    if (result < 0) {
        errno = (int)-result;
        return -1;
    }
    if (set) *set = (sigset_t)pending;
    return 0;
}

int sigsuspend(const sigset_t *mask) {
    sigset_t previous;
    if (sigprocmask(SIG_SETMASK, mask, &previous) < 0) return -1;
    pc_syscall(SYS_PAUSE, 0, 0, 0);
    sigprocmask(SIG_SETMASK, &previous, 0);
    errno = EINTR;
    return -1;
}

int kill(int pid, int signum) {
    int64_t result = pc_syscall(SYS_KILL, (uint64_t)(int64_t)pid, (uint64_t)(uint32_t)signum, 0);
    if (result < 0) {
        errno = (int)-result;
        return -1;
    }
    return 0;
}

int raise(int signum) {
    return kill((int)pc_getpid(), signum);
}

int getcontext(ucontext_t *context) {
    (void)context;
    errno = ENOSYS;
    return -1;
}
