#include <kernel/process/process.h>
#include <kernel/process/futex.h>
#include <kernel/process/scheduler.h>
#include "../common.h"

long sys_thread_create(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  struct kernel_thread_args *request =
      (struct kernel_thread_args *)(uintptr_t)a1;
  if (!readable(request, sizeof(*request)))
    return -KERR_FAULT;
  struct kernel_thread_args copy = *request;
  return process_thread_create(copy.entry, copy.stack, copy.argument,
                               copy.tls_base, copy.clear_tid);
}

long sys_thread_exit(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  return process_thread_exit(a1, a2);
}

long sys_futex(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a5;
  (void)a6;
  struct process *owner = process_current();
  if (!owner)
    return -KERR_PERM;
  if (a1 == FUTEX_WAIT)
    return futex_wait(owner->address_space, a2, (uint32_t)a3,
                      (uint32_t)a4);
  if (a1 == FUTEX_WAKE)
    return futex_wake(owner->address_space, a2,
                      a3 > INT32_MAX ? INT32_MAX : (int32_t)a3);
  return -KERR_INVAL;
}

long sys_set_fs_base(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (a1 >= 0x0000800000000000ULL)
    return -KERR_INVAL;
  scheduler_set_fs_base(a1);
  return 0;
}

long sys_gettid(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a1;
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  return scheduler_current_tid();
}

long sys_sched_yield(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a1;
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  scheduler_yield();
  return 0;
}

long sys_sleep(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (a1 > UINT32_MAX)
    return -1;
  scheduler_sleep((uint32_t)a1);
  return 0;
}
