#include <kernel/trace/trace.h>
#include <lib/string.h>
#include <kernel/ipc/ipc.h>
#include <kernel/process/process.h>
#include "../common.h"

long sys_fork(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  struct syscall_regs *r = syscall_current_regs();
  (void)a1;
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  return process_fork(r);
}

long sys_exec(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (!readable_string((const char *)(uintptr_t)a1) ||
      (a2 && !readable_string((const char *)(uintptr_t)a2)))
    return -1;
  if (a2 &&
      strlen((const char *)(uintptr_t)a2) >= PROCESS_COMMAND_LINE_CAPACITY)
    return -1;
  return process_spawn_module((const char *)(uintptr_t)a1,
                              (const char *)(uintptr_t)a2);
}

long sys_execve(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  struct syscall_regs *r = syscall_current_regs();
  (void)a4;
  (void)a5;
  (void)a6;
  if (!process_user_string((const char *)(uintptr_t)a1, 4096))
    return -KERR_FAULT;
  return process_execve(r, (const char *)(uintptr_t)a1,
                        (const char *const *)(uintptr_t)a2,
                        (const char *const *)(uintptr_t)a3);
}

long sys_exit(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  process_exit_current((int32_t)a1);
}

long sys_wait(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a4;
  (void)a5;
  (void)a6;
  if (a2 && !writable((void *)(uintptr_t)a2, sizeof(int32_t)))
    return -1;
  return process_wait((uint32_t)a1, (int32_t *)(uintptr_t)a2, a3 != 0);
}

long sys_waitpid(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a4;
  (void)a5;
  (void)a6;
  if (a2 && !writable((void *)(uintptr_t)a2, sizeof(int32_t)))
    return -KERR_FAULT;
  int32_t wait_status = 0;
  int32_t waited = process_waitpid((int32_t)a1, &wait_status, (uint32_t)a3);
  if (waited > 0 && a2)
    *(int32_t *)(uintptr_t)a2 = wait_status;
  return waited;
}

long sys_getpid(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a1;
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  return process_current_pid();
}

long sys_getppid(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a1;
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  return process_parent_pid((uint32_t)process_current_pid());
}

long sys_get_command_line(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (a2 > 0x7FFFFFFFULL ||
      !writable((void *)(uintptr_t)a1, (uint32_t)a2))
    return -1;
  return process_command_line((char *)(uintptr_t)a1, (uint32_t)a2);
}

long sys_get_process_name(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (a2 > 32 || !writable((void *)(uintptr_t)a1, (uint32_t)a2))
    return -1;
  return process_name((char *)(uintptr_t)a1, (uint32_t)a2);
}

long sys_env_get(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a4;
  (void)a5;
  (void)a6;
  if (a3 > PROCESS_ENVIRONMENT_VALUE_CAPACITY)
    return -1;
  if (!readable_string((const char *)(uintptr_t)a1) ||
      !writable((void *)(uintptr_t)a2, (uint32_t)a3))
    return -1;
  return process_environment_get((const char *)(uintptr_t)a1,
                                 (char *)(uintptr_t)a2, (uint32_t)a3);
}

long sys_env_set(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (!readable_string((const char *)(uintptr_t)a1) ||
      !readable_string((const char *)(uintptr_t)a2))
    return -1;
  return process_environment_set((const char *)(uintptr_t)a1,
                                 (const char *)(uintptr_t)a2);
}

long sys_env_unset(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (!readable_string((const char *)(uintptr_t)a1))
    return -1;
  return process_environment_unset((const char *)(uintptr_t)a1);
}

long sys_env_list(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (a2 > PROCESS_ENVIRONMENT_LIMIT)
    return -1;
  struct process_environment_variable *variables =
      (struct process_environment_variable *)(uintptr_t)a1;
  if (a2 && !writable(variables, a2 * sizeof(*variables)))
    return -1;
  struct process_environment_entry entries[PROCESS_ENVIRONMENT_LIMIT];
  int32_t count = process_environment_list(entries, (uint32_t)a2);
  if (count < 0)
    return count;
  uint32_t copied =
      (uint32_t)count < (uint32_t)a2 ? (uint32_t)count : (uint32_t)a2;
  for (uint32_t index = 0; index < copied; index++)
  {
    variables[index].used = entries[index].used ? 1 : 0;
    strncpy(variables[index].name, entries[index].name,
            sizeof(variables[index].name) - 1);
    variables[index].name[sizeof(variables[index].name) - 1] = '\0';
    strncpy(variables[index].value, entries[index].value,
            sizeof(variables[index].value) - 1);
    variables[index].value[sizeof(variables[index].value) - 1] = '\0';
  }
  return count;
}

long sys_process_list(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a4;
  (void)a5;
  (void)a6;
  if (a2 > 1024)
    return -1;
  struct process_monitor_info *entries =
      (struct process_monitor_info *)(uintptr_t)a1;
  if (a2 && !writable(entries, a2 * sizeof(*entries)))
    return -1;
  if (a3 > UINT32_MAX)
    return -1;
  return process_monitor_list_page(entries, (uint32_t)a2, (uint32_t)a3);
}

long sys_trace_attach(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  return trace_attach((uint32_t)a1, (uint32_t)a2);
}

long sys_trace_read(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a5;
  (void)a6;
  return trace_read((int32_t)a1, (struct trace_record *)(uintptr_t)a2, (uint32_t)a3, (uint32_t)a4);
}

long sys_trace_control(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a4;
  (void)a5;
  (void)a6;
  return trace_control((int32_t)a1, (uint32_t)a2, a3);
}

long sys_ipc_send(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a4;
  (void)a5;
  (void)a6;
  if (!a1 || !a2 || (uint32_t)a3 > IPC_MAX_PAYLOAD ||
      !readable((const void *)(uintptr_t)a2, (uint64_t)(uint32_t)a3))
    return -1;
  return ipc_send((uint32_t)a1, (const void *)(uintptr_t)a2,
                  (uint32_t)a3);
}

long sys_ipc_recv(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a4;
  (void)a5;
  (void)a6;
  if (!a1 || !a2 ||
      !writable((void *)(uintptr_t)a1, (uint64_t)(uint32_t)a2))
    return -1;
  return ipc_recv((void *)(uintptr_t)a1, (uint32_t)a2, (uint32_t)a3);
}
