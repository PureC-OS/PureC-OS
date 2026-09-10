#pragma once
// PureC hosted libc: signal. No signal delivery exists yet, so this is
// declarations plus ENOSYS stubs (see signal.c) — enough for TCC's
// -run backtrace sources to compile; AOT never arms handlers.

typedef void (*sighandler_t)(int);
typedef unsigned long sigset_t;

#define SIGHUP 1
#define SIGINT 2
#define SIGQUIT 3
#define SIGILL 4
#define SIGTRAP 5
#define SIGABRT 6
#define SIGBUS 7
#define SIGFPE 8
#define SIGKILL 9
#define SIGUSR1 10
#define SIGSEGV 11
#define SIGUSR2 12
#define SIGPIPE 13
#define SIGALRM 14
#define SIGTERM 15
#define SIGCHLD 17
#define SIGCONT 18
#define SIGSTOP 19
#define SIGTSTP 20
#define SIGTTIN 21
#define SIGTTOU 22

#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)
#define SIG_ERR ((sighandler_t)-1)

#define SA_SIGINFO 4
#define SA_RESTART 0x10000000
#define SA_NOCLDSTOP 1

// Minimal siginfo for TCC's -run backtrace sources to compile.
// Never populated on PureC OS (no delivery); handlers stay unarmed.
typedef struct {
    int si_signo;
    int si_code;
    void *si_addr;
} siginfo_t;

#define ILL_ILLOPC 1
#define FPE_INTDIV 1
#define FPE_FLTDIV 3
#define SEGV_MAPERR 1
#define SEGV_ACCERR 2
#define BUS_ADRALN 1

#define SIG_BLOCK 0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

struct sigaction {
    sighandler_t sa_handler;
    sigset_t sa_mask;
    int sa_flags;
    void (*sa_sigaction)(int, siginfo_t *, void *);
};

int sigprocmask(int how, const sigset_t *set, sigset_t *previous);

int signal(int signum, sighandler_t handler);
int sigaction(int signum, const struct sigaction *action,
              struct sigaction *previous);
int sigemptyset(sigset_t *set);
int sigaddset(sigset_t *set, int signum);
int sigdelset(sigset_t *set, int signum);
int sigismember(const sigset_t *set, int signum);
int raise(int signum);
int kill(int pid, int signum);
