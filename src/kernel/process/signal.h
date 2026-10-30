#pragma once

#include <stdbool.h>
#include <stdint.h>

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
#define SIGNAL_COUNT 32

#define SIGNAL_BLOCK 0
#define SIGNAL_UNBLOCK 1
#define SIGNAL_SETMASK 2

struct process;
struct syscall_regs;
struct ksigaction;

int32_t signal_send(uint32_t pid, int32_t sig);
void signal_raise_current(int32_t sig);
void signal_notify_child_exit(struct process *parent);
bool signal_pending_current(void);
void signal_deliver(struct syscall_regs *regs);
void signal_fault(int32_t sig, struct syscall_regs *regs);
int32_t signal_return(struct syscall_regs *regs);
int32_t signal_action_set(int32_t sig, const struct ksigaction *action,
                          struct ksigaction *previous);
int32_t signal_mask_change(int32_t how, const uint64_t *set, uint64_t *previous);
int32_t signal_pending_get(uint64_t *out);
void signal_exec_reset(struct process *process);
bool signal_clone(struct process *child, const struct process *parent);
void signal_free(struct process *process);
int32_t signal_pause(void);
