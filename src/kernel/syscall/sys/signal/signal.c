#include <arch/arch.h>
#include <kernel/ipc/pty.h>
#include <io/io_sched.h>
#include <kernel/syscall/syscall.h>
#include <kernel/tty/vtcon.h>
#include <drivers/audio/audio.h>
#include <drivers/display/gop.h>
#include <drivers/display/svga.h>
#include <drivers/display/svga3d.h>
#include <drivers/display/vga.h>
#include <drivers/input/keyboard.h>
#include <drivers/interrupts/timer.h>
#include <drivers/mouse/ps2_mouse.h>
#include <drivers/mouse/usb_mouse.h>
#include <drivers/power/power.h>
#include <drivers/serial/serial.h>
#include <io/storage/ahci.h>
#include <io/storage/block_device.h>
#include <io/storage/storage_probe.h>
#include <drivers/usb/ehci.h>
#include <drivers/usb/xhci.h>
#include <fs/install.h>
#include <fs/install_accounts.h>
#include <fs/vfs.h>
#include <fs/vfs_fat32.h>
#include <fs/vfs_path.h>
#include <drivers/display/display_owner.h>
#include <kernel/ipc/inputdev.h>
#include <kernel/syscall/sys/user/sudo.h>
#include <kernel/trace/trace.h>
#include <lib/string.h>
#include <mm/pmm.h>
#include <mm/shm.h>
#include <kernel/random/krandom.h>
#include <kernel/time/wallclock.h>
#include <net/api/ping.h>
#include <net/api/socket.h>
#include <net/api/sockstat.h>
#include <net/config/dhcp.h>
#include <net/core/net_device.h>
#include <net/name/dns.h>
#include <net/network/ipv4.h>
#include <net/transport/tcp.h>
#include <net/wifi/wifi.h>
#include <kernel/core/system_info.h>
#include <kernel/diagnostics/boot_diag.h>
#include <kernel/diagnostics/klog.h>
#include <kernel/diagnostics/panic.h>
#include <kernel/ipc/evfd.h>
#include <kernel/ipc/ipc.h>
#include <kernel/ipc/unix.h>
#include <kernel/process/process.h>
#include <kernel/process/signal.h>
#include <kernel/process/futex.h>
#include <kernel/process/fdtable.h>
#include <kernel/process/ofd.h>
#include <kernel/process/scheduler.h>
#include <kernel/smp/cpu.h>
#include <kernel/tty/tty.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
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
