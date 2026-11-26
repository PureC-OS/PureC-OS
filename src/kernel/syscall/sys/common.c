#include <kernel/syscall/sys/user/sudo.h>
#include <kernel/process/process.h>
#include <kernel/process/scheduler.h>
#include "common.h"

static volatile bool filesystem_syscall_busy;

bool privileged(uint32_t capability)
{
  return process_current_uid() == 0 || process_has_capability(capability);
}

bool wifi_allowed(void)
{
  return privileged(PROCESS_CAP_NET_ADMIN) || sudo_may_manage_network();
}

bool readable(const void *buffer, uint64_t size)
{
  return process_user_buffer(buffer, size, false);
}

bool writable(void *buffer, uint64_t size)
{
  return process_user_buffer(buffer, size, true);
}

bool readable_string(const char *text)
{
  return process_user_string(text, 4096);
}
void filesystem_syscall_lock(void)
{
  uint32_t spins = 0;
  while (__atomic_test_and_set(&filesystem_syscall_busy, __ATOMIC_ACQUIRE))
  {
    if (++spins < 8)
      scheduler_yield();
    else
      scheduler_sleep(1);
  }
}

void filesystem_syscall_unlock(void)
{
  __atomic_clear(&filesystem_syscall_busy, __ATOMIC_RELEASE);
}

bool filesystem_syscall_acquire(void)
{
  if (install_job.state == 1 &&
      scheduler_current_tid() != __atomic_load_n(&install_worker_tid, __ATOMIC_ACQUIRE))
    return false;
  filesystem_syscall_lock();
  return true;
}
