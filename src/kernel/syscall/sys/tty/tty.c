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

static int64_t tty_ioctl_request(uint32_t request, uint64_t argument)
{
  void *pointer = (void *)(uintptr_t)argument;
  switch (request)
  {
  case TTY_TCGETS:
    if (!writable(pointer, sizeof(struct tty_termios)))
      return -KERR_FAULT;
    tty_get_termios((struct tty_termios *)pointer);
    return 0;
  case TTY_TCSETS:
  case TTY_TCSETSW:
  case TTY_TCSETSF:
  {
    if (!readable(pointer, sizeof(struct tty_termios)))
      return -KERR_FAULT;
    struct tty_termios mode = *(const struct tty_termios *)pointer;
    tty_set_termios(&mode, request == TTY_TCSETSF,
                    (uint32_t)process_current_pid());
    return 0;
  }
  case TTY_TIOCGWINSZ:
    if (!writable(pointer, sizeof(struct tty_winsize)))
      return -KERR_FAULT;
    tty_get_winsize((struct tty_winsize *)pointer);
    return 0;
  case TTY_TIOCSWINSZ:
    return 0;
  case TTY_FIONREAD:
    if (!writable(pointer, sizeof(int32_t)))
      return -KERR_FAULT;
    *(int32_t *)pointer = (int32_t)tty_pending();
    return 0;
  case TTY_TCFLSH:
    if (argument == 0 || argument == 2)
      tty_flush_input();
    return 0;
  case TTY_RAW_ACTIVE:
    return tty_raw_mode() ? 1 : 0;
  default:
    return -KERR_NOTTY;
  }
}

long sys_tty_read(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (a2 > 0x7FFFFFFFULL || !writable((void *)(uintptr_t)a1, a2))
    return -KERR_FAULT;
  return tty_read((void *)(uintptr_t)a1, (uint32_t)a2);
}

long sys_tty_ioctl(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a4;
  (void)a5;
  (void)a6;
  int32_t pty_id;
  bool pty_master;
  int32_t object = fd_object((int32_t)a3);
  if (object >= 0 && ofd_pty_info(object, &pty_id, &pty_master))
  {
    uint32_t req = (uint32_t)a1;
    uint64_t need = req == TTY_TCGETS ? sizeof(struct tty_termios)
                    : (req == TTY_TCSETS || req == TTY_TCSETSW || req == TTY_TCSETSF) ? sizeof(struct tty_termios)
                    : (req == TTY_TIOCGWINSZ || req == TTY_TIOCSWINSZ) ? sizeof(struct tty_winsize)
                    : req == TTY_FIONREAD ? sizeof(int32_t) : 0;
    if (need && (req == TTY_TCGETS || req == TTY_TIOCGWINSZ || req == TTY_FIONREAD
                     ? !writable((void *)(uintptr_t)a2, need)
                     : !readable((const void *)(uintptr_t)a2, need)))
      return -KERR_FAULT;
    return pty_ioctl(pty_id, pty_master, req, a2);
  }
  return tty_ioctl_request((uint32_t)a1, a2);
}

long sys_openpty(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (!writable((void *)(uintptr_t)a1, 2 * sizeof(int32_t)))
    return -KERR_FAULT;
  int32_t pair[2];
  int32_t made = fd_openpty(pair, (uint32_t)a2);
  if (made < 0)
    return made;
  ((int32_t *)(uintptr_t)a1)[0] = pair[0];
  ((int32_t *)(uintptr_t)a1)[1] = pair[1];
  return 0;
}

long sys_vt_activate(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  struct process *self = process_current();
  if (!self)
    return -KERR_PERM;
  if (self->uid != 0)
  {
    int32_t object = fd_object(0);
    int32_t pty_id;
    bool master;
    if (object < 0 || !ofd_pty_info(object, &pty_id, &master) || vtcon_index_of_pty(pty_id) < 0)
      return -KERR_PERM;
  }
  return vtcon_activate((int32_t)a1) < 0 ? -KERR_BUSY : 0;
}

long sys_vt_active(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a1;
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  return vtcon_active();
}

long sys_getchar(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a1;
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  return (uint8_t)keyboard_getc();
}

long sys_try_getchar(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a1;
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  char character;
  return keyboard_try_getc(&character) ? (uint8_t)character : -1;
}

long sys_try_get_special(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a1;
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  uint8_t key;
  return keyboard_try_get_special(&key) ? key : -1;
}

long sys_key_event_get(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  struct key_event *out = (struct key_event *)(uintptr_t)a1;
  if (!writable(out, sizeof(*out)))
    return -1;
  return keyboard_try_get_event(out) ? 1 : 0;
}

long sys_get_mouse(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  struct mouse_state *out = (struct mouse_state *)(uintptr_t)a1;
  if (!writable(out, sizeof(*out)))
    return -1;
  ps2_mouse_poll();
  usb_mouse_poll();
  *out = mouse_get_state();
  return 0;
}

long sys_mouse_debug_get(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a1;
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  return mouse_get_debug_overlay() ? 1 : 0;
}

long sys_mouse_debug_set(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  mouse_set_debug_overlay(a1 != 0);
  return 0;
}

long sys_input_open(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a1;
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (display_owner_pid() != (uint32_t)process_current_pid())
    return -KERR_PERM;
  int32_t status = inputdev_open();
  if (status < 0)
    return status;
  int32_t object = ofd_create_input();
  if (object < 0)
  {
    inputdev_release();
    return object;
  }
  return fd_install_ofd(object, 0);
}
