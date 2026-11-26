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
