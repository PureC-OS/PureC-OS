#include <lib/string.h>
#include <kernel/process/signal.h>
#include "../common.h"

long sys_kill(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if ((int32_t)a1 <= 0)
    return -KERR_SRCH;
  return signal_send((uint32_t)a1, (int32_t)a2);
}

long sys_sigaction(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a4;
  (void)a5;
  (void)a6;
  if ((a2 && !readable((const void *)(uintptr_t)a2,
                       sizeof(struct ksigaction))) ||
      (a3 && !writable((void *)(uintptr_t)a3, sizeof(struct ksigaction))))
    return -KERR_FAULT;
  struct ksigaction incoming;
  if (a2)
    incoming = *(const struct ksigaction *)(uintptr_t)a2;
  struct ksigaction outgoing;
  int32_t changed = signal_action_set((int32_t)a1, a2 ? &incoming : 0,
                                      a3 ? &outgoing : 0);
  if (changed >= 0 && a3)
    *(struct ksigaction *)(uintptr_t)a3 = outgoing;
  return changed;
}

long sys_sigprocmask(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a4;
  (void)a5;
  (void)a6;
  if ((a2 && !readable((const void *)(uintptr_t)a2, sizeof(uint64_t))) ||
      (a3 && !writable((void *)(uintptr_t)a3, sizeof(uint64_t))))
    return -KERR_FAULT;
  uint64_t incoming = a2 ? *(const uint64_t *)(uintptr_t)a2 : 0;
  uint64_t outgoing = 0;
  int32_t changed = signal_mask_change((int32_t)a1, a2 ? &incoming : 0,
                                       a3 ? &outgoing : 0);
  if (changed >= 0 && a3)
    *(uint64_t *)(uintptr_t)a3 = outgoing;
  return changed;
}

long sys_sigpending(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (!writable((void *)(uintptr_t)a1, sizeof(uint64_t)))
    return -KERR_FAULT;
  uint64_t pending = 0;
  int32_t got = signal_pending_get(&pending);
  if (got >= 0)
    *(uint64_t *)(uintptr_t)a1 = pending;
  return got;
}

long sys_sigreturn(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  struct syscall_regs *r = syscall_current_regs();
  (void)a1;
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  return signal_return(r);
}

long sys_sigaltstack(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  struct syscall_regs *r = syscall_current_regs();
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  struct kernel_stack incoming;
  struct kernel_stack outgoing;
  if ((a1 && !readable((const void *)(uintptr_t)a1, sizeof(incoming))) ||
      (a2 && !writable((void *)(uintptr_t)a2, sizeof(outgoing))))
    return -KERR_FAULT;
  if (a1)
    memcpy(&incoming, (const void *)(uintptr_t)a1, sizeof(incoming));
  int32_t changed = signal_altstack(a1 ? &incoming : 0, a2 ? &outgoing : 0,
                                    r->rsp);
  if (changed >= 0 && a2)
    memcpy((void *)(uintptr_t)a2, &outgoing, sizeof(outgoing));
  return changed;
}

long sys_pause(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a1;
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  return signal_pause();
}
