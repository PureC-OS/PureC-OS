#include <kernel/process/process.h>
#include <kernel/syscall/syscall.h>
#include <kernel/syscall/syscall_table.h>
#include <drivers/serial/serial.h>

static void print_hex(uint64_t v)
{
  const char *h = "0123456789ABCDEF";
  char buf[17];
  buf[16] = 0;
  for (int i = 15; i >= 0; i--)
  {
    buf[i] = h[v & 0xF];
    v >>= 4;
  }
  serial_write_string("0x");
  serial_write_string(buf);
}

static struct syscall_regs kernel_regs;

struct syscall_regs *syscall_current_regs(void)
{
  struct process *p = process_current();
  if (p && p->syscall_regs)
    return p->syscall_regs;
  return &kernel_regs;
}

long x64_sys_call(const struct syscall_regs *regs, uint64_t nr)
{
  if (nr >= SYS_NR || !sys_call_table[nr])
  {
    serial_write_string("[SYSCALL] unknown n=");
    print_hex(nr);
    serial_write_string("\n");
    return -1;
  }
  return sys_call_table[nr](regs->rdi, regs->rsi, regs->rdx, regs->r10,
                            regs->r8, regs->r9);
}

int64_t do_syscall_64(struct syscall_regs *regs)
{
  struct process *p = process_current();
  struct syscall_regs *saved = 0;
  if (p)
  {
    saved = p->syscall_regs;
    p->syscall_regs = regs;
  }
  int64_t ret = x64_sys_call(regs, regs->rax);
  p = process_current();
  if (p)
    p->syscall_regs = saved;
  return ret;
}
