#include "signal.h"
#include "../../lib/string.h"
#include "../../mm/oom/oom_slab.h"
#include "../../mm/vmm.h"
#include "../diagnostics/klog.h"
#include "../syscall/syscall.h"
#include "process.h"
#include "scheduler.h"

#define SIG_BIT(sig) (1ULL << ((sig) - 1))
#define SIG_UNBLOCKABLE (SIG_BIT(SIGKILL) | SIG_BIT(SIGSTOP))
#define SIG_DEFAULT_IGNORED                                                    \
  (SIG_BIT(SIGCHLD) | SIG_BIT(SIGCONT) | SIG_BIT(SIGSTOP) | SIG_BIT(SIGTSTP) | \
   SIG_BIT(SIGTTIN) | SIG_BIT(SIGTTOU))
#define USER_CODE_SELECTOR 0x1B
#define USER_DATA_SELECTOR 0x23
#define USER_FLAGS_MASK 0x00000CD5ULL
#define USER_RED_ZONE 128ULL
#define USER_TOP_ADDRESS 0x0000800000000000ULL

struct process_signals {
  struct ksigaction actions[SIGNAL_COUNT + 1];
};

struct signal_frame {
  uint64_t return_address;
  uint64_t signo;
  uint64_t saved_mask;
  struct syscall_regs regs;
};

static bool signal_valid(int32_t sig) { return sig >= 1 && sig <= SIGNAL_COUNT; }

static struct process_signals *signals_ensure(struct process *process) {
  if (process->signals)
    return process->signals;
  struct process_signals *created =
      (struct process_signals *)oom_slab_alloc(sizeof(struct process_signals));
  if (!created)
    return 0;
  memset(created, 0, sizeof(*created));
  process->signals = created;
  return created;
}

void signal_free(struct process *process) {
  if (process && process->signals) {
    oom_slab_free(process->signals, sizeof(struct process_signals));
    process->signals = 0;
  }
}

bool signal_clone(struct process *child, const struct process *parent) {
  child->sig_blocked = parent->sig_blocked;
  child->sig_pending = 0;
  if (!parent->signals)
    return true;
  struct process_signals *copy = signals_ensure(child);
  if (!copy)
    return false;
  memcpy(copy, parent->signals, sizeof(*copy));
  return true;
}

void signal_exec_reset(struct process *process) {
  if (!process->signals)
    return;
  for (int32_t sig = 1; sig <= SIGNAL_COUNT; sig++) {
    struct ksigaction *action = &process->signals->actions[sig];
    if (action->handler != SIGNAL_ACTION_IGNORE)
      memset(action, 0, sizeof(*action));
  }
}

static bool action_ignored(const struct process *process, int32_t sig) {
  if (process->signals) {
    uint64_t handler = process->signals->actions[sig].handler;
    if (handler == SIGNAL_ACTION_IGNORE)
      return true;
    if (handler != SIGNAL_ACTION_DEFAULT)
      return false;
  }
  return (SIG_DEFAULT_IGNORED & SIG_BIT(sig)) != 0;
}

int32_t signal_send(uint32_t pid, int32_t sig) {
  if (sig < 0 || sig > SIGNAL_COUNT)
    return -KERR_INVAL;
  process_table_lock();
  struct process *target = process_lookup_by_pid(pid);
  if (!target || target->state == PROCESS_EXITED ||
      target->state == PROCESS_FREE) {
    process_table_unlock();
    return -KERR_SRCH;
  }
  struct process *caller = process_current();
  if (caller && caller->uid != 0 && caller->uid != target->uid) {
    process_table_unlock();
    return -KERR_PERM;
  }
  int32_t tid = target->thread_id;
  if (sig && (sig == SIGKILL || !action_ignored(target, sig))) {
    __atomic_fetch_or(&target->sig_pending, SIG_BIT(sig), __ATOMIC_SEQ_CST);
  }
  process_table_unlock();
  if (sig)
    scheduler_unblock(tid);
  return 0;
}

void signal_notify_child_exit(struct process *parent) {
  if (!action_ignored(parent, SIGCHLD) ||
      (parent->signals &&
       parent->signals->actions[SIGCHLD].handler != SIGNAL_ACTION_IGNORE &&
       parent->signals->actions[SIGCHLD].handler != SIGNAL_ACTION_DEFAULT))
    __atomic_fetch_or(&parent->sig_pending, SIG_BIT(SIGCHLD),
                      __ATOMIC_SEQ_CST);
}

void signal_raise_current(int32_t sig) {
  struct process *process = process_current();
  if (!process || !signal_valid(sig))
    return;
  if (sig == SIGKILL || !action_ignored(process, sig))
    __atomic_fetch_or(&process->sig_pending, SIG_BIT(sig), __ATOMIC_SEQ_CST);
}

static uint64_t deliverable(const struct process *process) {
  uint64_t pending = __atomic_load_n(&process->sig_pending, __ATOMIC_SEQ_CST);
  return pending & (~process->sig_blocked | SIG_UNBLOCKABLE);
}

bool signal_pending_current(void) {
  struct process *process = process_current();
  if (!process)
    return false;
  uint64_t ready = deliverable(process);
  if (!ready)
    return false;
  for (int32_t sig = 1; sig <= SIGNAL_COUNT; sig++) {
    if ((ready & SIG_BIT(sig)) && !(action_ignored(process, sig) &&
                                    sig != SIGKILL))
      return true;
  }
  return false;
}

static bool frame_writable(uint64_t address, uint64_t size) {
  if (address >= USER_TOP_ADDRESS || size > USER_TOP_ADDRESS - address)
    return false;
  for (uint64_t page = address & ~4095ULL; page < address + size;
       page += 4096)
    (void)process_handle_stack_fault(page);
  return process_user_buffer((const void *)(uintptr_t)address, size, true);
}

static void deliver_handler(struct process *process, struct syscall_regs *regs,
                            int32_t sig, const struct ksigaction *action) {
  uint64_t frame_size = (sizeof(struct signal_frame) + 15) & ~15ULL;
  uint64_t top = (regs->rsp - USER_RED_ZONE) & ~15ULL;
  uint64_t sp = top - frame_size - 8;
  if (!frame_writable(sp, sizeof(struct signal_frame)) ||
      action->restorer >= USER_TOP_ADDRESS || !action->restorer) {
    process_exit_signal(SIGSEGV);
  }
  struct signal_frame *frame = (struct signal_frame *)(uintptr_t)sp;
  frame->return_address = action->restorer;
  frame->signo = (uint64_t)sig;
  frame->saved_mask = process->sig_blocked;
  frame->regs = *regs;
  process->sig_blocked |= action->mask;
  if (!(action->flags & SIGNAL_FLAG_NODEFER))
    process->sig_blocked |= SIG_BIT(sig);
  process->sig_blocked &= ~SIG_UNBLOCKABLE;
  if (action->flags & SIGNAL_FLAG_RESETHAND && process->signals)
    memset(&process->signals->actions[sig], 0, sizeof(struct ksigaction));
  regs->rip = action->handler;
  regs->rsp = sp;
  regs->rdi = (uint64_t)sig;
  regs->rsi = 0;
  regs->rdx = 0;
  regs->rax = 0;
}

void signal_deliver(struct syscall_regs *regs) {
  struct process *process = process_current();
  if (!process || !regs)
    return;
  for (;;) {
    uint64_t ready = deliverable(process);
    if (!ready)
      return;
    int32_t sig = 0;
    for (int32_t candidate = 1; candidate <= SIGNAL_COUNT; candidate++) {
      if (ready & SIG_BIT(candidate)) {
        sig = candidate;
        break;
      }
    }
    if (!sig)
      return;
    __atomic_fetch_and(&process->sig_pending, ~SIG_BIT(sig), __ATOMIC_SEQ_CST);
    if (sig == SIGKILL)
      process_exit_signal(SIGKILL);
    struct ksigaction action = {0};
    if (process->signals)
      action = process->signals->actions[sig];
    if (action.handler == SIGNAL_ACTION_IGNORE)
      continue;
    if (action.handler == SIGNAL_ACTION_DEFAULT) {
      if (SIG_DEFAULT_IGNORED & SIG_BIT(sig))
        continue;
      process_exit_signal(sig);
    }
    if (action.handler >= USER_TOP_ADDRESS)
      process_exit_signal(SIGSEGV);
    deliver_handler(process, regs, sig, &action);
    return;
  }
}

void signal_fault(int32_t sig, struct syscall_regs *regs) {
  struct process *process = process_current();
  if (!process || !signal_valid(sig))
    process_exit_signal(sig > 0 ? sig : SIGSEGV);
  bool handled = process->signals &&
                 process->signals->actions[sig].handler > SIGNAL_ACTION_IGNORE &&
                 !(process->sig_blocked & SIG_BIT(sig));
  if (!handled)
    process_exit_signal(sig);
  __atomic_fetch_or(&process->sig_pending, SIG_BIT(sig), __ATOMIC_SEQ_CST);
  signal_deliver(regs);
}

int32_t signal_return(struct syscall_regs *regs) {
  struct process *process = process_current();
  if (!process)
    return -KERR_INVAL;
  uint64_t base = regs->rsp - 8;
  if (base >= USER_TOP_ADDRESS ||
      !process_user_buffer((const void *)(uintptr_t)base,
                           sizeof(struct signal_frame), false))
    process_exit_signal(SIGSEGV);
  struct signal_frame frame = *(const struct signal_frame *)(uintptr_t)base;
  if (frame.regs.rip >= USER_TOP_ADDRESS || frame.regs.rsp >= USER_TOP_ADDRESS)
    process_exit_signal(SIGSEGV);
  process->sig_blocked = frame.saved_mask & ~SIG_UNBLOCKABLE;
  *regs = frame.regs;
  regs->cs = USER_CODE_SELECTOR;
  regs->ss = USER_DATA_SELECTOR;
  regs->rflags = (frame.regs.rflags & USER_FLAGS_MASK) | 0x202ULL;
  return (int64_t)frame.regs.rax;
}

int32_t signal_action_set(int32_t sig, const struct ksigaction *action,
                          struct ksigaction *previous) {
  struct process *process = process_current();
  if (!process || !signal_valid(sig))
    return -KERR_INVAL;
  if (action && (sig == SIGKILL || sig == SIGSTOP))
    return -KERR_INVAL;
  struct process_signals *table = signals_ensure(process);
  if (!table)
    return -KERR_NOMEM;
  if (previous)
    *previous = table->actions[sig];
  if (action) {
    table->actions[sig] = *action;
    if (action->handler == SIGNAL_ACTION_IGNORE)
      __atomic_fetch_and(&process->sig_pending, ~SIG_BIT(sig),
                         __ATOMIC_SEQ_CST);
  }
  return 0;
}

int32_t signal_mask_change(int32_t how, const uint64_t *set,
                           uint64_t *previous) {
  struct process *process = process_current();
  if (!process)
    return -KERR_INVAL;
  if (previous)
    *previous = process->sig_blocked;
  if (!set)
    return 0;
  uint64_t value = *set & ~SIG_UNBLOCKABLE;
  switch (how) {
  case SIGNAL_BLOCK:
    process->sig_blocked |= value;
    break;
  case SIGNAL_UNBLOCK:
    process->sig_blocked &= ~value;
    break;
  case SIGNAL_SETMASK:
    process->sig_blocked = value;
    break;
  default:
    return -KERR_INVAL;
  }
  return 0;
}

int32_t signal_pending_get(uint64_t *out) {
  struct process *process = process_current();
  if (!process || !out)
    return -KERR_INVAL;
  *out = process->sig_pending & process->sig_blocked;
  return 0;
}

int32_t signal_pause(void) {
  for (;;) {
    if (signal_pending_current())
      return -KERR_INTR;
    scheduler_sleep(10);
  }
}
