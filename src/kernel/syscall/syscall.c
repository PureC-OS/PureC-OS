#include "../ipc/pty.h"
#include "../../io/io_sched.h"
#include "syscall.h"
#include "../../drivers/audio/audio.h"
#include "../../drivers/display/gop.h"
#include "../../drivers/display/vga.h"
#include "../../drivers/input/keyboard.h"
#include "../../drivers/interrupts/timer.h"
#include "../../drivers/mouse/ps2_mouse.h"
#include "../../drivers/mouse/usb_mouse.h"
#include "../../drivers/power/power.h"
#include "../../drivers/serial/serial.h"
#include "../../io/storage/ahci.h"
#include "../../io/storage/block_device.h"
#include "../../io/storage/storage_probe.h"
#include "../../drivers/usb/ehci.h"
#include "../../drivers/usb/xhci.h"
#include "../../fs/install.h"
#include "../../fs/install_accounts.h"
#include "../../fs/vfs.h"
#include "../../drivers/display/display_owner.h"
#include "../ipc/inputdev.h"
#include "sudo.h"
#include "../../gfx/blit.h"
#include "../../gfx/compositor.h"
#include "../../lib/string.h"
#include "../../mm/pmm.h"
#include "../../mm/shm.h"
#include "../random/krandom.h"
#include "../time/wallclock.h"
#include "../../net/api/ping.h"
#include "../../net/api/socket.h"
#include "../../net/config/dhcp.h"
#include "../../net/core/net_device.h"
#include "../../net/name/dns.h"
#include "../../net/network/ipv4.h"
#include "../../net/transport/tcp.h"
#include "../../net/wifi/wifi.h"
#include "../../userspace/display_mode.h"
#include "../../userspace/rs_session.h"
#include "../../userspace/userspace.h"
#include "../../userspace/window_manager.h"
#include "../core/system_info.h"
#include "../diagnostics/boot_diag.h"
#include "../diagnostics/klog.h"
#include "../diagnostics/panic.h"
#include "../ipc/evfd.h"
#include "../ipc/ipc.h"
#include "../ipc/unix.h"
#include "../process/process.h"
#include "../process/signal.h"
#include "../process/futex.h"
#include "../process/fdtable.h"
#include "../process/ofd.h"
#include "../process/scheduler.h"
#include "../smp/cpu.h"
#include "../tty/tty.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define INSTALL_WORKER_PRIORITY 3

static volatile bool filesystem_syscall_busy;
static struct install_status install_job;
static struct install_log install_history;
static char install_device[STORAGE_DEVICE_NAME_CAPACITY];
static char install_serial[STORAGE_SERIAL_CAPACITY];

static bool readable(const void *buffer, uint64_t size)
{
  return process_user_buffer(buffer, size, false);
}

static bool writable(void *buffer, uint64_t size)
{
  return process_user_buffer(buffer, size, true);
}

static bool readable_string(const char *text)
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

static volatile int install_worker_tid = -1;

bool install_job_running(void)
{
  return install_job.state == 1;
}

bool filesystem_syscall_acquire(void)
{
  if (install_job.state == 1 &&
      scheduler_current_tid() != __atomic_load_n(&install_worker_tid, __ATOMIC_ACQUIRE))
    return false;
  filesystem_syscall_lock();
  return true;
}

static void install_progress(uint32_t progress, const char *stage)
{
  uint64_t flags;
  __asm__ volatile("pushfq; pop %0; cli" : "=r"(flags)::"memory");
  install_job.progress = progress;
  if (stage)
    strncpy(install_job.stage, stage, sizeof(install_job.stage) - 1);
  install_job.stage[sizeof(install_job.stage) - 1] = '\0';
  if (stage)
  {
    if (install_history.count == INSTALL_LOG_CAPACITY)
    {
      for (uint32_t index = 1; index < INSTALL_LOG_CAPACITY; index++)
        install_history.entries[index - 1] = install_history.entries[index];
      install_history.count--;
    }
    struct install_log_entry *entry =
        &install_history.entries[install_history.count++];
    entry->progress = progress;
    strncpy(entry->stage, stage, sizeof(entry->stage) - 1);
    entry->stage[sizeof(entry->stage) - 1] = '\0';
  }
  if (flags & (1ULL << 9))
    __asm__ volatile("sti" ::: "memory");
}

static void install_worker(void *argument)
{
  (void)argument;
  __atomic_store_n(&install_worker_tid, scheduler_current_tid(), __ATOMIC_RELEASE);
  filesystem_syscall_lock();
  block_device_begin_exclusive_io();
  io_bulk_begin();
  int32_t result = installer_install_device(install_device, install_serial,
                                            install_progress);
  io_bulk_end();
  block_device_end_exclusive_io();
  filesystem_syscall_unlock();
  __atomic_store_n(&install_worker_tid, -1, __ATOMIC_RELEASE);
  install_job.result = result;
  if (result < 0)
  {
    install_job.state = 3;
    char failed_stage[INSTALL_STAGE_CAPACITY];
    const char *prefix = "Failed: ";
    uint32_t length = 0;
    while (prefix[length] && length + 1 < sizeof(failed_stage))
    {
      failed_stage[length] = prefix[length];
      length++;
    }
    for (uint32_t index = 0;
         install_job.stage[index] && length + 1 < sizeof(failed_stage); index++)
      failed_stage[length++] = install_job.stage[index];
    failed_stage[length] = '\0';
    install_progress(install_job.progress, failed_stage);
  }
  else
  {
    install_job.state = 2;
    install_progress(92, "Disk layout complete");
  }
}

void install_report_ui_crash(int32_t status)
{
  if (install_job.state != 0)
    return;
  memset(&install_job, 0, sizeof(install_job));
  memset(&install_history, 0, sizeof(install_history));
  install_job.state = 3;
  install_job.result = status;
  install_progress(0, "Installer UI crashed");
}

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

#define POLL_MAX_DESCRIPTORS 1024
static uint32_t poll_overshoot_reports;

static int64_t poll_descriptors(struct kernel_pollfd *fds, uint64_t count,
                                int64_t timeout_ms)
{
  if (count > POLL_MAX_DESCRIPTORS ||
      (count && !writable(fds, count * sizeof(*fds))))
    return -KERR_FAULT;
  uint64_t started = timer_ticks();
  for (;;)
  {
    int64_t ready = 0;
    for (uint64_t i = 0; i < count; i++)
    {
      int32_t revents = 0;
      if (fds[i].fd >= 0)
      {
        revents = fd_poll(fds[i].fd);
        revents &= (uint16_t)fds[i].events | KPOLLERR | KPOLLHUP | KPOLLNVAL;
      }
      fds[i].revents = (int16_t)revents;
      if (revents)
        ready++;
    }
    if (ready || !timeout_ms)
      return ready;
    if (timeout_ms > 0 && timer_ticks() - started >= (uint64_t)timeout_ms)
    {
      uint64_t over = timer_ticks() - started - (uint64_t)timeout_ms;
      if (over >= 20 && poll_overshoot_reports < 64)
      {
        poll_overshoot_reports++;
        klogf(KLOG_INFO, "poll: timeout %d ms overshot by %u ms",
              (int32_t)timeout_ms, (uint32_t)over);
      }
      return 0;
    }
    if (signal_pending_current())
      return -KERR_INTR;
    scheduler_sleep(1);
  }
}

static int32_t socket_of_fd(int32_t fd)
{
  int32_t id = fd_socket_id(fd);
  if (id >= 0)
    return id;
  return fd_ctl(fd, FD_CTL_KIND, 0) < 0 ? -KERR_BADF : -KERR_NOTSOCK;
}

static int64_t socket_copy_address(uint64_t address_pointer,
                                   uint64_t length_pointer,
                                   const struct sock_addr_in *address)
{
  if (!address_pointer || !length_pointer)
    return 0;
  if (!writable((void *)(uintptr_t)length_pointer, sizeof(uint32_t)))
    return -KERR_FAULT;
  uint32_t capacity = *(uint32_t *)(uintptr_t)length_pointer;
  uint32_t copy = capacity < sizeof(*address) ? capacity : (uint32_t)sizeof(*address);
  if (copy && !writable((void *)(uintptr_t)address_pointer, copy))
    return -KERR_FAULT;
  if (copy)
    memcpy((void *)(uintptr_t)address_pointer, address, copy);
  *(uint32_t *)(uintptr_t)length_pointer = sizeof(*address);
  return 0;
}

static int64_t socket_read_address(uint64_t pointer, uint64_t length,
                                   struct sock_addr_in *out)
{
  if (length < 8 || !readable((const void *)(uintptr_t)pointer, length > sizeof(*out) ? sizeof(*out) : length))
    return -KERR_FAULT;
  memset(out, 0, sizeof(*out));
  memcpy(out, (const void *)(uintptr_t)pointer, length > sizeof(*out) ? sizeof(*out) : length);
  return 0;
}


#define MSG_IOV_MAX 32
#define UNIX_ADDRESS_FAMILY 1

static int32_t unix_of_fd(int32_t fd)
{
  int32_t object = fd_object(fd);
  return object < 0 ? -1 : ofd_unix_id(object);
}

static int64_t unix_copy_address(uint64_t address_pointer,
                                 uint64_t length_pointer, const uint8_t *name,
                                 uint32_t length)
{
  if (!address_pointer || !length_pointer)
    return 0;
  if (!writable((void *)(uintptr_t)length_pointer, sizeof(uint32_t)))
    return -KERR_FAULT;
  uint32_t capacity = *(uint32_t *)(uintptr_t)length_pointer;
  uint8_t buffer[2 + UNIX_PATH_MAX + 1] = {UNIX_ADDRESS_FAMILY, 0};
  if (length > UNIX_PATH_MAX + 1)
    length = UNIX_PATH_MAX + 1;
  memcpy(buffer + 2, name, length);
  uint32_t total = 2 + length;
  uint32_t copy = capacity < total ? capacity : total;
  if (copy && !writable((void *)(uintptr_t)address_pointer, copy))
    return -KERR_FAULT;
  if (copy)
    memcpy((void *)(uintptr_t)address_pointer, buffer, copy);
  *(uint32_t *)(uintptr_t)length_pointer = total;
  return 0;
}

static int64_t unix_read_address(uint64_t pointer, uint64_t length,
                                 uint8_t *name, uint32_t *name_length)
{
  if (length < 2 || length > 2 + UNIX_PATH_MAX)
    return -KERR_INVAL;
  if (!readable((const void *)(uintptr_t)pointer, length))
    return -KERR_FAULT;
  const uint8_t *source = (const uint8_t *)(uintptr_t)pointer;
  if (source[0] != UNIX_ADDRESS_FAMILY || source[1] != 0)
    return -KERR_AFNOSUPPORT;
  *name_length = (uint32_t)(length - 2);
  memcpy(name, source + 2, *name_length);
  return 0;
}

static int64_t unix_socket_syscall(uint64_t n, int32_t fd, int32_t id,
                                   uint64_t a2, uint64_t a3, uint64_t a4)
{
  bool nonblock = fd_nonblocking(fd);
  switch (n)
  {
  case SYS_BIND:
  case SYS_CONNECT:
  {
    uint8_t name[UNIX_PATH_MAX + 1];
    uint32_t length = 0;
    int64_t status = unix_read_address(a2, a3, name, &length);
    if (status < 0)
      return status;
    if (n == SYS_BIND)
      return unix_bind(id, name, length);
    return unix_connect(id, name, length, nonblock);
  }
  case SYS_LISTEN:
    return unix_listen(id, (int32_t)a2);
  case SYS_ACCEPT:
  {
    int32_t child;
    int32_t result = unix_accept(id, nonblock, &child);
    if (result < 0)
      return result;
    uint8_t name[UNIX_PATH_MAX + 1];
    int32_t length = unix_name(child, true, name, sizeof(name));
    int32_t opened = fd_install_unix(
        child, (uint32_t)a4 & (SOCK_FLAG_NONBLOCK | SOCK_FLAG_CLOEXEC));
    if (opened < 0)
      return opened;
    (void)unix_copy_address(a2, a3, name, length < 0 ? 0 : (uint32_t)length);
    return opened;
  }
  case SYS_SOCK_SHUTDOWN:
    return unix_shutdown(id, (int32_t)a2);
  case SYS_SOCKNAME:
  {
    uint8_t name[UNIX_PATH_MAX + 1];
    int32_t length = unix_name(id, a4 != 0, name, sizeof(name));
    if (length < 0)
      return length;
    return unix_copy_address(a2, a3, name, (uint32_t)length);
  }
  case SYS_SETSOCKOPT:
    return 0;
  case SYS_GETSOCKOPT:
  {
    if (!writable((void *)(uintptr_t)a4, sizeof(uint32_t)))
      return -KERR_FAULT;
    uint32_t length = *(uint32_t *)(uintptr_t)a4;
    if (length > 64 || (length && !writable((void *)(uintptr_t)a3, length)))
      return -KERR_FAULT;
    uint8_t value[64] = {0};
    uint32_t value_length = sizeof(int32_t);
    int32_t level = (int32_t)(a2 >> 32);
    int32_t name = (int32_t)(uint32_t)a2;
    if (level != KSOL_SOCKET)
      return -KERR_NOPROTOOPT;
    if (name == SOCK_SO_TYPE)
      *(int32_t *)value = UNIX_TYPE_STREAM;
    else if (name == SOCK_SO_ERROR)
      *(int32_t *)value = 0;
    else if (name == SOCK_SO_SNDBUF || name == SOCK_SO_RCVBUF)
      *(int32_t *)value = 16384;
    else if (name == KSO_PEERCRED)
    {
      struct unix_cred cred;
      int32_t status = unix_peer_cred(id, &cred);
      if (status < 0)
        return status;
      memcpy(value, &cred, sizeof(cred));
      value_length = sizeof(cred);
    }
    else
      return -KERR_NOPROTOOPT;
    if (value_length < length)
      length = value_length;
    memcpy((void *)(uintptr_t)a3, value, length);
    *(uint32_t *)(uintptr_t)a4 = length;
    return 0;
  }
  case SYS_SOCK_SEND:
  case SYS_SOCK_RECV:
  {
    if (a3 > 0x7FFFFFFFULL)
      return -KERR_INVAL;
    struct unix_iov iov = {a2, a3};
    if (n == SYS_SOCK_SEND)
    {
      if (!readable((const void *)(uintptr_t)a2, a3))
        return -KERR_FAULT;
      return unix_send(id, &iov, 1, 0, 0, nonblock, (uint32_t)a4);
    }
    if (!writable((void *)(uintptr_t)a2, a3))
      return -KERR_FAULT;
    return unix_recv(id, &iov, 1, 0, 0, 0, 0, nonblock, (uint32_t)a4);
  }
  default:
    return -KERR_NOSYS;
  }
}

static int64_t message_iov_load(const struct kernel_msghdr *msg,
                                struct unix_iov *iov, bool for_write)
{
  if (msg->iov_count > MSG_IOV_MAX)
    return -KERR_INVAL;
  if (msg->iov_count &&
      !readable((const void *)(uintptr_t)msg->iov, msg->iov_count * sizeof(*iov)))
    return -KERR_FAULT;
  if (msg->iov_count)
    memcpy(iov, (const void *)(uintptr_t)msg->iov, msg->iov_count * sizeof(*iov));
  uint64_t total = 0;
  for (uint64_t i = 0; i < msg->iov_count; i++)
  {
    if (iov[i].length > 0x7FFFFFFFULL)
      return -KERR_INVAL;
    total += iov[i].length;
    if (total > 0x7FFFFFFFULL)
      return -KERR_INVAL;
    if (!iov[i].length)
      continue;
    bool ok = for_write ? writable((void *)(uintptr_t)iov[i].base, iov[i].length)
                        : readable((const void *)(uintptr_t)iov[i].base, iov[i].length);
    if (!ok)
      return -KERR_FAULT;
  }
  return 0;
}

static int64_t sendmsg_syscall(int32_t fd, uint64_t message_pointer,
                               uint32_t flags)
{
  struct kernel_msghdr msg;
  struct unix_iov iov[MSG_IOV_MAX];
  if (!readable((const void *)(uintptr_t)message_pointer, sizeof(msg)))
    return -KERR_FAULT;
  memcpy(&msg, (const void *)(uintptr_t)message_pointer, sizeof(msg));
  int64_t loaded = message_iov_load(&msg, iov, false);
  if (loaded < 0)
    return loaded;
  bool nonblock = fd_nonblocking(fd);
  int32_t object = fd_object(fd);
  if (object < 0)
    return -KERR_BADF;
  int32_t unix_id = ofd_unix_id(object);
  if (unix_id >= 0)
  {
    int32_t ofds[UNIX_FDS_PER_MESSAGE];
    uint32_t ofd_count = 0;
    if (msg.control && msg.control_length >= sizeof(struct kernel_cmsghdr))
    {
      if (!readable((const void *)(uintptr_t)msg.control, msg.control_length))
        return -KERR_FAULT;
      const uint8_t *control = (const uint8_t *)(uintptr_t)msg.control;
      uint64_t offset = 0;
      int64_t failure = 0;
      while (!failure && offset + sizeof(struct kernel_cmsghdr) <= msg.control_length)
      {
        struct kernel_cmsghdr header;
        memcpy(&header, control + offset, sizeof(header));
        if (header.length < sizeof(header) ||
            offset + header.length > msg.control_length)
        {
          failure = -KERR_INVAL;
          break;
        }
        if (header.level == KSOL_SOCKET && header.type == KSCM_RIGHTS)
        {
          uint64_t count = (header.length - sizeof(header)) / sizeof(int32_t);
          for (uint64_t i = 0; i < count; i++)
          {
            int32_t passed;
            memcpy(&passed, control + offset + sizeof(header) + i * sizeof(int32_t),
                   sizeof(passed));
            int32_t passed_object = fd_object(passed);
            if (passed_object < 0)
            {
              failure = -KERR_BADF;
              break;
            }
            if (ofd_count >= UNIX_FDS_PER_MESSAGE)
            {
              failure = -KERR_INVAL;
              break;
            }
            ofd_ref(passed_object);
            ofds[ofd_count++] = passed_object;
          }
        }
        offset += (header.length + 7) & ~7ULL;
      }
      if (failure)
      {
        for (uint32_t i = 0; i < ofd_count; i++)
          ofd_release(ofds[i]);
        return failure;
      }
    }
    int64_t sent = unix_send(unix_id, iov, (uint32_t)msg.iov_count, ofds,
                             ofd_count, nonblock, flags);
    if (sent <= 0)
      for (uint32_t i = 0; i < ofd_count; i++)
        ofd_release(ofds[i]);
    return sent;
  }
  int32_t socket_id = ofd_socket_id(object);
  if (socket_id < 0)
    return -KERR_NOTSOCK;
  int64_t total = 0;
  for (uint64_t i = 0; i < msg.iov_count; i++)
  {
    if (!iov[i].length)
      continue;
    int32_t result = sock_write(socket_id, (const void *)(uintptr_t)iov[i].base,
                                (uint32_t)iov[i].length, nonblock, flags);
    if (result < 0)
      return total ? total : result;
    total += result;
    if ((uint64_t)result < iov[i].length)
      break;
  }
  return total;
}

static int64_t recvmsg_syscall(int32_t fd, uint64_t message_pointer,
                               uint32_t flags)
{
  struct kernel_msghdr msg;
  struct unix_iov iov[MSG_IOV_MAX];
  if (!writable((void *)(uintptr_t)message_pointer, sizeof(msg)))
    return -KERR_FAULT;
  memcpy(&msg, (const void *)(uintptr_t)message_pointer, sizeof(msg));
  int64_t loaded = message_iov_load(&msg, iov, true);
  if (loaded < 0)
    return loaded;
  if (msg.control && msg.control_length &&
      !writable((void *)(uintptr_t)msg.control, msg.control_length))
    return -KERR_FAULT;
  bool nonblock = fd_nonblocking(fd);
  int32_t object = fd_object(fd);
  if (object < 0)
    return -KERR_BADF;
  int32_t unix_id = ofd_unix_id(object);
  int64_t received;
  int32_t out_flags = 0;
  uint64_t control_used = 0;
  if (unix_id >= 0)
  {
    int32_t ofds[UNIX_FDS_PER_MESSAGE];
    uint32_t ofd_count = 0;
    bool truncated = false;
    uint32_t capacity = 0;
    if (msg.control && msg.control_length > sizeof(struct kernel_cmsghdr))
    {
      capacity = (uint32_t)((msg.control_length - sizeof(struct kernel_cmsghdr)) /
                            sizeof(int32_t));
      if (capacity > UNIX_FDS_PER_MESSAGE)
        capacity = UNIX_FDS_PER_MESSAGE;
    }
    received = unix_recv(unix_id, iov, (uint32_t)msg.iov_count, ofds,
                         &ofd_count, &truncated, capacity, nonblock, flags);
    if (received < 0)
      return received;
    if (ofd_count)
    {
      uint32_t installed = 0;
      int32_t fds[UNIX_FDS_PER_MESSAGE];
      uint32_t install_flags =
          (flags & KMSG_CMSG_CLOEXEC) ? SOCK_FLAG_CLOEXEC : 0;
      for (uint32_t i = 0; i < ofd_count; i++)
      {
        int32_t installed_fd = fd_install_ofd(ofds[i], install_flags);
        if (installed_fd < 0)
          truncated = true;
        else
          fds[installed++] = installed_fd;
      }
      if (installed)
      {
        struct kernel_cmsghdr header = {
            sizeof(header) + installed * sizeof(int32_t), KSOL_SOCKET,
            KSCM_RIGHTS};
        uint8_t *control = (uint8_t *)(uintptr_t)msg.control;
        memcpy(control, &header, sizeof(header));
        memcpy(control + sizeof(header), fds, installed * sizeof(int32_t));
        control_used = (header.length + 7) & ~7ULL;
      }
    }
    if (truncated)
      out_flags |= KMSG_CTRUNC;
  }
  else
  {
    int32_t socket_id = ofd_socket_id(object);
    if (socket_id < 0)
      return -KERR_NOTSOCK;
    received = 0;
    for (uint64_t i = 0; i < msg.iov_count; i++)
    {
      if (!iov[i].length)
        continue;
      received = sock_read(socket_id, (void *)(uintptr_t)iov[i].base,
                           (uint32_t)iov[i].length, nonblock, flags);
      break;
    }
    if (received < 0)
      return received;
  }
  msg.name_length = 0;
  msg.control_length = control_used;
  msg.flags = out_flags;
  memcpy((void *)(uintptr_t)message_pointer, &msg, sizeof(msg));
  return received;
}

static int64_t socketpair_syscall(uint64_t domain, uint64_t type_flags,
                                  uint64_t protocol, uint64_t pair_pointer)
{
  (void)protocol;
  if (domain != UNIX_ADDRESS_FAMILY)
    return -KERR_AFNOSUPPORT;
  if (!writable((void *)(uintptr_t)pair_pointer, 2 * sizeof(int32_t)))
    return -KERR_FAULT;
  int32_t ids[2];
  int32_t status = unix_pair((int32_t)(type_flags & SOCK_TYPE_MASK), ids);
  if (status < 0)
    return status;
  uint32_t flags = (uint32_t)type_flags & (SOCK_FLAG_NONBLOCK | SOCK_FLAG_CLOEXEC);
  int32_t first = fd_install_unix(ids[0], flags);
  if (first < 0)
  {
    unix_release(ids[1]);
    return first;
  }
  int32_t second = fd_install_unix(ids[1], flags);
  if (second < 0)
  {
    (void)fd_close(first);
    return second;
  }
  int32_t *out = (int32_t *)(uintptr_t)pair_pointer;
  out[0] = first;
  out[1] = second;
  return 0;
}

static int64_t socket_syscall(uint64_t n, uint64_t a1, uint64_t a2, uint64_t a3,
                              uint64_t a4, uint64_t a5)
{
  int32_t fd = (int32_t)a1;
  if (n != SYS_SOCKET && n != SYS_SOCKETPAIR && n != SYS_SENDMSG &&
      n != SYS_RECVMSG)
  {
    int32_t unix_id = unix_of_fd(fd);
    if (unix_id >= 0)
    {
      if (n == SYS_SENDTO)
        n = SYS_SOCK_SEND;
      else if (n == SYS_RECVFROM)
        n = SYS_SOCK_RECV;
      return unix_socket_syscall(n, fd, unix_id, a2, a3, a4);
    }
  }
  switch (n)
  {
  case SYS_SENDTO:
  case SYS_RECVFROM:
  {
    int32_t id = socket_of_fd(fd);
    if (id < 0)
      return id;
    if (a3 > 0x7FFFFFFFULL)
      return -KERR_INVAL;
    struct sock_addr_in address;
    if (n == SYS_SENDTO)
    {
      if (!readable((const void *)(uintptr_t)a2, a3))
        return -KERR_FAULT;
      if (a5)
      {
        if (!readable((const void *)(uintptr_t)a5, sizeof(address)))
          return -KERR_FAULT;
        memcpy(&address, (const void *)(uintptr_t)a5, sizeof(address));
      }
      return sock_sendto(id, (const void *)(uintptr_t)a2, (uint32_t)a3,
                         fd_nonblocking(fd), (uint32_t)a4, a5 ? &address : 0);
    }
    if (!writable((void *)(uintptr_t)a2, a3))
      return -KERR_FAULT;
    if (a5 && !writable((void *)(uintptr_t)a5, sizeof(address)))
      return -KERR_FAULT;
    int32_t got = sock_recvfrom(id, (void *)(uintptr_t)a2, (uint32_t)a3,
                                fd_nonblocking(fd), (uint32_t)a4, &address);
    if (got >= 0 && a5)
      memcpy((void *)(uintptr_t)a5, &address, sizeof(address));
    return got;
  }
  case SYS_SOCKETPAIR:
    return socketpair_syscall(a1, a2, a3, a4);
  case SYS_SENDMSG:
    return sendmsg_syscall(fd, a2, (uint32_t)a3);
  case SYS_RECVMSG:
    return recvmsg_syscall(fd, a2, (uint32_t)a3);
  case SYS_SOCKET:
  {
    uint32_t type_flags = (uint32_t)a2;
    if (a1 == UNIX_ADDRESS_FAMILY)
    {
      int32_t unix_id = unix_create((int32_t)(type_flags & SOCK_TYPE_MASK));
      if (unix_id < 0)
        return unix_id;
      return fd_install_unix(
          unix_id, type_flags & (SOCK_FLAG_NONBLOCK | SOCK_FLAG_CLOEXEC));
    }
    int32_t id = sock_create((int32_t)a1, (int32_t)(type_flags & SOCK_TYPE_MASK),
                             (int32_t)a3);
    if (id < 0)
      return id;
    int32_t opened = fd_install_socket(
        id, type_flags & (SOCK_FLAG_NONBLOCK | SOCK_FLAG_CLOEXEC));
    if (opened < 0)
      sock_release(id);
    return opened;
  }
  case SYS_CONNECT:
  case SYS_BIND:
  {
    int32_t id = socket_of_fd(fd);
    if (id < 0)
      return id;
    struct sock_addr_in address;
    int64_t copied = socket_read_address(a2, a3, &address);
    if (copied < 0)
      return copied;
    if (n == SYS_BIND)
      return sock_bind(id, &address);
    return sock_connect(id, &address, fd_nonblocking(fd));
  }
  case SYS_LISTEN:
  {
    int32_t id = socket_of_fd(fd);
    return id < 0 ? id : sock_listen(id, (int32_t)a2);
  }
  case SYS_ACCEPT:
  {
    int32_t id = socket_of_fd(fd);
    if (id < 0)
      return id;
    uint32_t flags = (uint32_t)a4;
    struct sock_addr_in peer;
    int32_t child;
    int32_t result = sock_accept(id, fd_nonblocking(fd), &child, &peer);
    if (result < 0)
      return result;
    int32_t opened = fd_install_socket(
        child, flags & (SOCK_FLAG_NONBLOCK | SOCK_FLAG_CLOEXEC));
    if (opened < 0)
    {
      sock_release(child);
      return opened;
    }
    (void)socket_copy_address(a2, a3, &peer);
    return opened;
  }
  case SYS_SOCK_SHUTDOWN:
  {
    int32_t id = socket_of_fd(fd);
    return id < 0 ? id : sock_shutdown(id, (int32_t)a2);
  }
  case SYS_SOCKNAME:
  {
    int32_t id = socket_of_fd(fd);
    if (id < 0)
      return id;
    struct sock_addr_in address;
    int32_t result = sock_name(id, a4 != 0, &address);
    if (result < 0)
      return result;
    return socket_copy_address(a2, a3, &address);
  }
  case SYS_SETSOCKOPT:
  {
    int32_t id = socket_of_fd(fd);
    if (id < 0)
      return id;
    if (a4 > 64 || (a4 && !readable((const void *)(uintptr_t)a3, a4)))
      return -KERR_FAULT;
    uint8_t value[64];
    if (a4)
      memcpy(value, (const void *)(uintptr_t)a3, a4);
    return sock_setopt(id, (int32_t)(a2 >> 32), (int32_t)(uint32_t)a2, value,
                       (uint32_t)a4);
  }
  case SYS_GETSOCKOPT:
  {
    int32_t id = socket_of_fd(fd);
    if (id < 0)
      return id;
    if (!writable((void *)(uintptr_t)a4, sizeof(uint32_t)))
      return -KERR_FAULT;
    uint32_t length = *(uint32_t *)(uintptr_t)a4;
    if (length > 64 || (length && !writable((void *)(uintptr_t)a3, length)))
      return -KERR_FAULT;
    uint8_t value[64] = {0};
    int32_t result = sock_getopt(id, (int32_t)(a2 >> 32), (int32_t)(uint32_t)a2,
                                 value, &length);
    if (result < 0)
      return result;
    memcpy((void *)(uintptr_t)a3, value, length);
    *(uint32_t *)(uintptr_t)a4 = length;
    return 0;
  }
  case SYS_SOCK_SEND:
  case SYS_SOCK_RECV:
  {
    int32_t id = socket_of_fd(fd);
    if (id < 0)
      return id;
    if (a3 > 0x7FFFFFFFULL)
      return -KERR_INVAL;
    if (n == SYS_SOCK_SEND)
    {
      if (!readable((const void *)(uintptr_t)a2, a3))
        return -KERR_FAULT;
      return sock_write(id, (const void *)(uintptr_t)a2, (uint32_t)a3,
                        fd_nonblocking(fd), (uint32_t)a4);
    }
    if (!writable((void *)(uintptr_t)a2, a3))
      return -KERR_FAULT;
    return sock_read(id, (void *)(uintptr_t)a2, (uint32_t)a3,
                     fd_nonblocking(fd), (uint32_t)a4);
  }
  default:
    return -KERR_NOSYS;
  }
}

int64_t syscall_handler(struct syscall_regs *r)
{
  uint64_t n = r->rax;
  uint64_t a1 = r->rbx;
  uint64_t a2 = r->rcx;
  uint64_t a3 = r->rdx;
  switch (n)
  {
  case SYS_WRITE:
  {
    const char *s = (const char *)(uintptr_t)a1;
    uint64_t len = a2;
    if (!readable(s, len))
      return -1;
    struct process *writer = process_current();
    if (writer && writer->descriptors[1] != VFS_FD_STDOUT)
    {
      if (len > 0x7FFFFFFFULL)
        return -1;
      int32_t sent = fd_write(1, s, (uint32_t)len);
      return sent < 0 ? -1 : (int64_t)sent;
    }
    for (uint64_t i = 0; i < len; i++)
    {
      serial_putc(s[i]);
      if (gop_console_is_active())
        gop_console_putc(s[i]);
      else if (!rs_session_active())
        gop_putc(s[i]);
    }
    return (int64_t)len;
  }
  case SYS_CLEAR:
  {
    uint32_t color = (uint32_t)a1;
    gop_clear(color);
    if (!gop_is_available())
      vga_clear();
    return 0;
  }
  case SYS_DRAW_RECT:
  {
    uint32_t x = (uint32_t)a1, y = (uint32_t)a2, w = (uint32_t)a3,
             h = (uint32_t)r->rsi;
    uint32_t c = (uint32_t)r->rdi;
    if (compositor_draw_rect((uint32_t)process_current_pid(), x, y, w, h, c))
      return 0;
    gop_draw_rect(x, y, w, h, c);
    return 0;
  }
  case SYS_DRAW_LINE:
  {
    uint32_t x0 = (uint32_t)a1, y0 = (uint32_t)a2, x1 = (uint32_t)a3,
             y1 = (uint32_t)r->rsi;
    uint32_t c = (uint32_t)r->rdi;
    if (compositor_draw_line((uint32_t)process_current_pid(), x0, y0, x1, y1,
                             c))
      return 0;
    gop_draw_line(x0, y0, x1, y1, c);
    return 0;
  }
  case SYS_FB_INFO:
  {
    struct framebuffer_info *info = (struct framebuffer_info *)(uintptr_t)a1;
    if (!writable(info, sizeof(*info)))
      return -1;
    memset(info, 0, sizeof(*info));
    info->width = gop_get_width();
    info->height = gop_get_height();
    info->pitch = gop_get_pitch();
    info->size_bytes = gop_get_framebuffer_size_bytes();
    info->bpp = gop_get_bpp();
    info->available = gop_is_available() ? 1 : 0;
    strncpy(info->protocol_name, gop_get_protocol_name(),
            sizeof(info->protocol_name) - 1);
    return 0;
  }
  case SYS_SCROLL_RECT_UP:
  {
    struct framebuffer_scroll_request *request =
        (struct framebuffer_scroll_request *)(uintptr_t)a1;
    if (!readable(request, sizeof(*request)))
      return -1;
    if (readable(request, sizeof(*request)) &&
        compositor_scroll_rect_up((uint32_t)process_current_pid(), request->x,
                                  request->y, request->w, request->h,
                                  request->amount, request->fill_color))
      return 0;
    gop_scroll_rect_up(request->x, request->y, request->w, request->h,
                       request->amount, request->fill_color);
    return 0;
  }

  case SYS_FB_BEGIN_UPDATE:
    if (compositor_begin_update((uint32_t)process_current_pid()))
      return 0;
    mouse_begin_framebuffer_update();
    return 0;
  case SYS_FB_END_UPDATE:
    if (compositor_end_update((uint32_t)process_current_pid()))
      return 0;
    mouse_end_framebuffer_update();
    return 0;
  case SYS_DISPLAY_SET_MODE:
    return display_mode_apply((uint32_t)a1, (uint32_t)a2, (uint8_t)a3,
                              (uint32_t)r->rsi);
  case SYS_DISPLAY_SCALE_INFO:
  {
    struct display_scale_info *info =
        (struct display_scale_info *)(uintptr_t)a1;
    if (!writable(info, sizeof(*info)))
      return -1;
    info->physical_width = gop_get_physical_width();
    info->physical_height = gop_get_physical_height();
    info->scale_percent = gop_get_scale_percent();
    info->reserved = 0;
    return 0;
  }
  case SYS_CONSOLE_CONFIGURE:
  {
    const struct framebuffer_console_request *request =
        (const struct framebuffer_console_request *)(uintptr_t)a1;
    if (!readable(request, sizeof(*request)))
      return -1;
    return gop_console_configure_owner((uint32_t)process_current_pid(),
                                       request->x, request->y, request->width,
                                       request->height, request->foreground,
                                       request->background)
               ? 0
               : -1;
  }
  case SYS_CONSOLE_CLEAR:
    if (!gop_console_is_active())
      return -1;
    gop_console_clear();
    return 0;
  case SYS_CONSOLE_DISABLE:
    gop_console_disable();
    return 0;
  case SYS_DESKTOP_REDRAW:
    userspace_redraw_desktop();
    return 0;
  case SYS_GUI_WINDOW_REGISTER:
  {
    const struct gui_window_request *request =
        (const struct gui_window_request *)(uintptr_t)a1;
    if (!readable(request, sizeof(*request)))
      return -1;
    if (!window_manager_register((uint32_t)process_current_pid(), request))
      return -1;
    scheduler_set_affinity(scheduler_current_tid(), 0);
    return 0;
  }
  case SYS_GUI_WINDOW_UPDATE:
  {
    const struct gui_window_request *request =
        (const struct gui_window_request *)(uintptr_t)a1;
    if (!readable(request, sizeof(*request)))
      return -1;
    return window_manager_update((uint32_t)process_current_pid(), request) ? 0
                                                                           : -1;
  }
  case SYS_GUI_WINDOW_UNREGISTER:
    window_manager_unregister((uint32_t)process_current_pid());
    return 0;
  case SYS_GUI_WINDOW_INFO:
  {
    struct gui_window_info info;
    if (!writable((void *)(uintptr_t)a1, sizeof(info)))
      return -1;
    if (!window_manager_info((uint32_t)process_current_pid(), &info))
      return -1;
    *(struct gui_window_info *)(uintptr_t)a1 = info;
    return 0;
  }
  case SYS_GUI_WINDOW_FLAGS:
    return window_manager_set_flags((uint32_t)process_current_pid(),
                                    (uint32_t)a1)
               ? 0
               : -1;
  case SYS_TTY_READ:
    if (a2 > 0x7FFFFFFFULL || !writable((void *)(uintptr_t)a1, a2))
      return -KERR_FAULT;
    return tty_read((void *)(uintptr_t)a1, (uint32_t)a2);
  case SYS_TTY_IOCTL:
  {
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
  case SYS_OPENPTY:
  {
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
  case SYS_MMAP:
  {
    const uint64_t legacy_size = offsetof(struct kernel_mmap_args, fd);
    if (!readable((const void *)(uintptr_t)a1, legacy_size))
      return -KERR_FAULT;
    struct kernel_mmap_args request;
    memset(&request, 0, sizeof(request));
    memcpy(&request, (const void *)(uintptr_t)a1, legacy_size);
    if (!(request.flags & KMAP_SHARED))
      return process_mmap(request.address, request.length, request.prot,
                          request.flags);
    if (!readable((const void *)(uintptr_t)a1, sizeof(request)))
      return -KERR_FAULT;
    memcpy(&request, (const void *)(uintptr_t)a1, sizeof(request));
    if (request.offset & (PMM_PAGE_SIZE - 1))
      return -KERR_INVAL;
    int32_t shm;
    if (request.fd < 0)
    {
      if (!request.length)
        return -KERR_INVAL;
      shm = shm_create((request.length + PMM_PAGE_SIZE - 1) &
                       ~(PMM_PAGE_SIZE - 1));
      if (shm < 0)
        return shm;
    }
    else
    {
      int32_t object = fd_object((int32_t)request.fd);
      if (object < 0)
        return -KERR_BADF;
      shm = ofd_shm_id(object);
      if (shm < 0)
        return -KERR_NODEV;
      shm_ref(shm);
    }
    int64_t mapped = process_mmap_shared(request.address, request.length,
                                         request.prot, request.flags, shm,
                                         request.offset / PMM_PAGE_SIZE);
    shm_unref(shm);
    return mapped;
  }
  case SYS_MEMFD_CREATE:
  {
    if (!readable_string((const char *)(uintptr_t)a1))
      return -KERR_FAULT;
    int32_t shm = shm_create(0);
    if (shm < 0)
      return shm;
    return fd_install_shm(shm, (a2 & 1) ? 02000000U : 0U);
  }
  case SYS_SHM_OPEN:
  {
    if (!readable_string((const char *)(uintptr_t)a1))
      return -KERR_FAULT;
    char name[SHM_NAME_MAX + 1];
    uint64_t length = strlen((const char *)(uintptr_t)a1);
    if (length > SHM_NAME_MAX)
      return -KERR_NAMETOOLONG;
    memcpy(name, (const void *)(uintptr_t)a1, length + 1);
    int32_t shm;
    int32_t status = shm_open_named(name, (a2 & 0100) != 0, (a2 & 0200) != 0, &shm);
    if (status < 0)
      return status;
    if (a2 & 01000)
      (void)shm_truncate(shm, 0);
    return fd_install_shm(shm, (uint32_t)a2 & 02000000U);
  }
  case SYS_SHM_UNLINK:
  {
    if (!readable_string((const char *)(uintptr_t)a1))
      return -KERR_FAULT;
    return shm_unlink_named((const char *)(uintptr_t)a1);
  }
  case SYS_MADVISE:
    return process_madvise(a1, a2, a3);
  case SYS_SIGALTSTACK:
  {
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
  case SYS_MUNMAP:
    return process_munmap(a1, a2);
  case SYS_MPROTECT:
    return process_mprotect(a1, a2, a3);
  case SYS_MREMAP:
    return process_mremap(a1, a2, a3, r->rsi);
  case SYS_POLL:
    return poll_descriptors((struct kernel_pollfd *)(uintptr_t)a1, a2,
                            (int64_t)(int32_t)a3);
  case SYS_EVENTFD:
  {
    int32_t id = evfd_eventfd_create(a1, (uint32_t)a2);
    return id < 0 ? id : fd_install_event(id, (uint32_t)a2);
  }
  case SYS_TIMERFD_CREATE:
  {
    int32_t id = evfd_timerfd_create((int32_t)a1);
    return id < 0 ? id : fd_install_event(id, (uint32_t)a2);
  }
  case SYS_TIMERFD_SETTIME:
  {
    struct kernel_itimerspec value;
    struct kernel_itimerspec old;
    struct kernel_itimerspec *old_user = (struct kernel_itimerspec *)(uintptr_t)r->rsi;
    if (!readable((const void *)(uintptr_t)a3, sizeof(value)) ||
        (old_user && !writable(old_user, sizeof(old))))
      return -KERR_FAULT;
    memcpy(&value, (const void *)(uintptr_t)a3, sizeof(value));
    int32_t object = fd_object((int32_t)a1);
    if (object < 0 || ofd_kind_of(object) != FD_KIND_EVENT)
      return -KERR_BADF;
    int32_t result = evfd_timerfd_settime(ofd_event_id(object), (uint32_t)a2,
                                          &value, old_user ? &old : 0);
    if (result == 0 && old_user)
      memcpy(old_user, &old, sizeof(old));
    return result;
  }
  case SYS_TIMERFD_GETTIME:
  {
    struct kernel_itimerspec value;
    if (!writable((void *)(uintptr_t)a2, sizeof(value)))
      return -KERR_FAULT;
    int32_t object = fd_object((int32_t)a1);
    if (object < 0 || ofd_kind_of(object) != FD_KIND_EVENT)
      return -KERR_BADF;
    int32_t result = evfd_timerfd_gettime(ofd_event_id(object), &value);
    if (result == 0)
      memcpy((void *)(uintptr_t)a2, &value, sizeof(value));
    return result;
  }
  case SYS_EPOLL_CREATE:
  {
    int32_t id = evfd_epoll_create();
    return id < 0 ? id : fd_install_event(id, (uint32_t)a1);
  }
  case SYS_EPOLL_CTL:
  {
    struct kernel_epoll_event event;
    memset(&event, 0, sizeof(event));
    if (a2 != EPOLL_OP_DEL)
    {
      if (!readable((const void *)(uintptr_t)r->rsi, sizeof(event)))
        return -KERR_FAULT;
      memcpy(&event, (const void *)(uintptr_t)r->rsi, sizeof(event));
    }
    int32_t object = fd_object((int32_t)a1);
    if (object < 0)
      return -KERR_BADF;
    if (ofd_kind_of(object) != FD_KIND_EVENT)
      return -KERR_INVAL;
    return evfd_epoll_ctl(ofd_event_id(object), (int32_t)a2, (int32_t)a3,
                          &event);
  }
  case SYS_EPOLL_WAIT:
  {
    int32_t max = (int32_t)a3;
    struct kernel_epoll_event ready[64];
    if (max <= 0)
      return -KERR_INVAL;
    uint32_t bounded = max > 64 ? 64U : (uint32_t)max;
    if (!writable((void *)(uintptr_t)a2, bounded * sizeof(ready[0])))
      return -KERR_FAULT;
    int32_t object = fd_object((int32_t)a1);
    if (object < 0)
      return -KERR_BADF;
    if (ofd_kind_of(object) != FD_KIND_EVENT)
      return -KERR_INVAL;
    int32_t count = evfd_epoll_wait(ofd_event_id(object), ready, (int32_t)bounded,
                                    (int64_t)(int32_t)r->rsi);
    if (count > 0)
      memcpy((void *)(uintptr_t)a2, ready, (uint32_t)count * sizeof(ready[0]));
    return count;
  }
  case SYS_GUI_WINDOW_TITLE:
  {
    if (!readable_string((const char *)(uintptr_t)a1))
      return -1;
    return window_manager_set_title((uint32_t)process_current_pid(),
                                    (const char *)(uintptr_t)a1)
               ? 0
               : -1;
  }
  case SYS_GUI_WINDOW_CMD:
    return window_manager_command((uint32_t)process_current_pid(),
                                  (uint32_t)a1)
               ? 0
               : -1;
  case SYS_GUI_WINDOW_STATE:
    return window_manager_state((uint32_t)process_current_pid());
  case SYS_GUI_WINDOW_REPAINT_DONE:
    window_manager_finish_repaint((uint32_t)process_current_pid());
    return 0;
  case SYS_GETPID:
    return process_current_pid();
  case SYS_GETRANDOM:
  {
    if (a2 > 0x100000 || !writable((void *)(uintptr_t)a1, a2))
      return -1;
    krandom_fill((void *)(uintptr_t)a1, (uint32_t)a2);
    return (int64_t)a2;
  }
  case SYS_FB_BLIT:
  {
    if (!readable((const void *)(uintptr_t)a1, sizeof(struct fb_blit_request)))
      return -1;
    struct fb_blit_request req =
        *(const struct fb_blit_request *)(uintptr_t)a1;
    uint64_t bytes;
    if (!blit_source_bytes(req.stride, req.width, req.height, &bytes))
      return -1;
    if (!readable((const void *)(uintptr_t)req.pixels, bytes))
      return -1;
    if (!compositor_blit((uint32_t)process_current_pid(), req.x, req.y,
                         req.width, req.height,
                         (const uint32_t *)(uintptr_t)req.pixels, req.stride,
                         req.flags))
      return -2;
    return 0;
  }
  case SYS_DESKTOP_CLAIM:
    return rs_session_claim((uint32_t)process_current_pid(),
                            process_current_uid());
  case SYS_DESKTOP_BLIT:
  {
    if (!readable((const void *)(uintptr_t)a1, sizeof(struct fb_blit_request)))
      return -1;
    struct fb_blit_request req =
        *(const struct fb_blit_request *)(uintptr_t)a1;
    uint64_t bytes;
    if (!blit_source_bytes(req.stride, req.width, req.height, &bytes))
      return -1;
    if (!readable((const void *)(uintptr_t)req.pixels, bytes))
      return -1;
    return rs_session_blit((uint32_t)process_current_pid(), &req);
  }
  case SYS_DESKTOP_WAIT:
    return rs_session_wait((uint32_t)process_current_pid(), (uint32_t)a1);
  case SYS_DESKTOP_WM:
    return rs_session_wm((uint32_t)process_current_pid(), a1, a2, a3);
  case SYS_TIME_GET:
  {
    if (!writable((void *)(uintptr_t)a1, sizeof(struct time_info)))
      return -1;
    struct time_info *info = (struct time_info *)(uintptr_t)a1;
    int64_t ms = wallclock_unix_ms();
    info->unix_seconds = ms / 1000;
    info->millis = (uint32_t)(ms % 1000);
    info->tz_minutes = wallclock_tz_minutes();
    info->uptime_ms = system_info_uptime_ms();
    info->source = wallclock_source();
    info->reserved = 0;
    return 0;
  }
  case SYS_TIME_SET:
    if (process_current_uid() != 0)
      return -1;
    return wallclock_set((int64_t)a1, (int32_t)a2);
  case SYS_TIME_ZONE:
  {
    if (a1 == TIME_ZONE_GET)
    {
      char *out = (char *)(uintptr_t)a2;
      uint32_t cap = (uint32_t)a3;
      if (cap == 0 || cap > 256 || !writable(out, cap))
        return -1;
      const char *name = wallclock_zone_name();
      uint32_t n = 0;
      while (name[n] && n + 1 < cap)
      {
        out[n] = name[n];
        n++;
      }
      out[n] = 0;
      return (int64_t)n;
    }
    if (a1 != TIME_ZONE_SET || process_current_uid() != 0 ||
        !readable_string((const char *)(uintptr_t)a2))
      return -1;
    char name[64];
    strncpy(name, (const char *)(uintptr_t)a2, sizeof(name) - 1);
    name[sizeof(name) - 1] = 0;
    if (!wallclock_set_zone(name))
      return -1;
    if (!filesystem_syscall_acquire())
      return 0;
    if (vfs_is_root_mounted())
    {
      char line[66];
      uint32_t n = (uint32_t)strlen(name);
      memcpy(line, name, n);
      line[n++] = '\n';
      if (vfs_write_file(WALLCLOCK_ZONE_PATH, line, n) < 0)
        klog(KLOG_WARN, "time: cannot write " WALLCLOCK_ZONE_PATH);
    }
    filesystem_syscall_unlock();
    return 0;
  }
  case SYS_FILE_STAT_EX:
  {
    if (!readable_string((const char *)(uintptr_t)a1) ||
        !writable((void *)(uintptr_t)a2, sizeof(struct file_stat_ex)))
      return -1;
    if (!filesystem_syscall_acquire())
      return -1;
    int32_t result = vfs_stat_ex((const char *)(uintptr_t)a1,
                                 (struct file_stat_ex *)(uintptr_t)a2);
    filesystem_syscall_unlock();
    return result;
  }
  case SYS_SESSION_BEGIN:
    return process_session_begin((uint32_t)a1, (uint32_t)a2);
  case SYS_GETUID:
    return (int64_t)process_current_uid();
  case SYS_GETGID:
    return (int64_t)process_current_gid();
  case SYS_DISPLAY_CLAIM:
  {
    if (!process_has_capability(PROCESS_CAP_DISPLAY) && process_current_uid() != 0)
      return -KERR_PERM;
    return display_owner_claim((uint32_t)process_current_pid());
  }
  case SYS_DISPLAY_RELEASE:
    display_owner_release((uint32_t)process_current_pid());
    return 0;
  case SYS_FB_MAP:
  {
    struct fb_map_info *out = (struct fb_map_info *)(uintptr_t)a1;
    if (!writable(out, sizeof(*out)))
      return -KERR_FAULT;
    if (display_owner_pid() != (uint32_t)process_current_pid())
      return -KERR_PERM;
    struct gop_front_info front;
    if (!gop_front_physical(&front) || front.bpp != 32)
      return -KERR_NODEV;
    uint64_t base = front.physical & ~0xFFFULL;
    uint64_t skew = front.physical - base;
    int64_t mapped = process_map_device(base, front.bytes + skew);
    if (mapped < 0)
      return mapped;
    struct fb_map_info info;
    memset(&info, 0, sizeof(info));
    info.address = (uint64_t)mapped + skew;
    info.bytes = front.bytes;
    info.width = front.width;
    info.height = front.height;
    info.pitch_bytes = front.pitch_bytes;
    info.bpp = front.bpp;
    info.generation = front.generation;
    *out = info;
    return 0;
  }
  case SYS_INPUT_OPEN:
  {
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
  case SYS_FB_DAMAGE:
    if (display_owner_pid() != (uint32_t)process_current_pid())
      return -KERR_PERM;
    gop_user_damage((uint32_t)a1, (uint32_t)a2, (uint32_t)a3, (uint32_t)r->rsi);
    return 0;
  case SYS_SUDO:
  {
    if (!readable_string((const char *)(uintptr_t)a1))
      return -KERR_FAULT;
    if (!filesystem_syscall_acquire())
      return -1;
    int32_t result = sudo_elevate((const char *)(uintptr_t)a1);
    filesystem_syscall_unlock();
    if (result < 0)
      scheduler_sleep(1500);
    return result;
  }
  case SYS_SETUID:
    return process_set_uid((uint32_t)a1);
  case SYS_SETGID:
    return process_set_gid((uint32_t)a1);
  case SYS_CHMOD:
  {
    if (!readable_string((const char *)(uintptr_t)a1))
      return -1;
    if (!filesystem_syscall_acquire())
      return -1;
    int32_t result = vfs_chmod((const char *)(uintptr_t)a1, (uint16_t)a2);
    filesystem_syscall_unlock();
    return result;
  }
  case SYS_CHOWN:
  {
    if (!readable_string((const char *)(uintptr_t)a1))
      return -1;
    if (!filesystem_syscall_acquire())
      return -1;
    int32_t result =
        vfs_chown((const char *)(uintptr_t)a1, (uint32_t)a2, (uint32_t)a3);
    filesystem_syscall_unlock();
    return result;
  }
  case SYS_UTIMES:
  {
    if (!readable_string((const char *)(uintptr_t)a1))
      return -1;
    char absolute[512];
    int32_t resolved = process_resolve_path((const char *)(uintptr_t)a1,
                                            absolute, sizeof(absolute));
    if (resolved < 0)
      return resolved;
    if (!filesystem_syscall_acquire())
      return -1;
    int32_t result = vfs_utimes(absolute, (uint32_t)a2, (uint32_t)a3);
    filesystem_syscall_unlock();
    if (result >= 0)
      return 0;
    switch (result)
    {
    case FS_ERROR_NOT_FOUND:
      return -KERR_NOENT;
    case FS_ERROR_DENIED:
      return -KERR_ACCES;
    case FS_ERROR_READ_ONLY:
      return -30;
    case FS_ERROR_UNSUPPORTED:
      return -KERR_NOSYS;
    case FS_ERROR_INVALID:
      return -KERR_INVAL;
    default:
      return -KERR_IO;
    }
  }
  case SYS_FILE_PERM:
  {
    if (!readable_string((const char *)(uintptr_t)a1) ||
        !writable((void *)(uintptr_t)a2, sizeof(struct file_perm_info)))
      return -1;
    if (!filesystem_syscall_acquire())
      return -1;
    int32_t result = vfs_get_perm((const char *)(uintptr_t)a1,
                                  (struct file_perm_info *)(uintptr_t)a2);
    filesystem_syscall_unlock();
    return result;
  }
  case SYS_USER_LOOKUP:
  {
    if ((a1 && !readable_string((const char *)(uintptr_t)a1)) ||
        !writable((void *)(uintptr_t)a3, sizeof(struct user_info)))
      return -1;
    if (!filesystem_syscall_acquire())
      return -1;
    int32_t result = vfs_user_lookup((const char *)(uintptr_t)a1, (uint32_t)a2,
                                     (struct user_info *)(uintptr_t)a3);
    filesystem_syscall_unlock();
    return result;
  }
  case SYS_HEAP_GROW:
  {
    uint64_t address = process_heap_grow(a1);
    return address ? (int64_t)address : -1;
  }
  case SYS_EXECVE:
    if (!process_user_string((const char *)(uintptr_t)a1, 4096))
      return -KERR_FAULT;
    return process_execve(r, (const char *)(uintptr_t)a1,
                          (const char *const *)(uintptr_t)a2,
                          (const char *const *)(uintptr_t)a3);
  case SYS_WAITPID:
  {
    if (a2 && !writable((void *)(uintptr_t)a2, sizeof(int32_t)))
      return -KERR_FAULT;
    int32_t wait_status = 0;
    int32_t waited = process_waitpid((int32_t)a1, &wait_status, (uint32_t)a3);
    if (waited > 0 && a2)
      *(int32_t *)(uintptr_t)a2 = wait_status;
    return waited;
  }
  case SYS_PIPE:
  {
    if (!writable((void *)(uintptr_t)a1, 2 * sizeof(int32_t)))
      return -KERR_FAULT;
    int32_t pair[2];
    int32_t made = fd_pipe(pair, (uint32_t)a2);
    if (made < 0)
      return made;
    ((int32_t *)(uintptr_t)a1)[0] = pair[0];
    ((int32_t *)(uintptr_t)a1)[1] = pair[1];
    return 0;
  }
  case SYS_DUP:
    return fd_dup((int32_t)a1, 0, false);
  case SYS_DUP2:
    return fd_dup2((int32_t)a1, (int32_t)a2);
  case SYS_FD_READ:
    if (a3 > 0x7FFFFFFFULL ||
        !writable((void *)(uintptr_t)a2, a3))
      return -KERR_FAULT;
    return fd_read((int32_t)a1, (void *)(uintptr_t)a2, (uint32_t)a3);
  case SYS_FD_WRITE:
    if (a3 > 0x7FFFFFFFULL || !readable((const void *)(uintptr_t)a2, a3))
      return -KERR_FAULT;
    return fd_write((int32_t)a1, (const void *)(uintptr_t)a2, (uint32_t)a3);
  case SYS_FD_OPEN:
  {
    if (!readable_string((const char *)(uintptr_t)a1))
      return -KERR_FAULT;
    char absolute[512];
    int32_t resolved =
        process_resolve_path((const char *)(uintptr_t)a1, absolute,
                             sizeof(absolute));
    if (resolved < 0)
      return resolved;
    return fd_open(absolute, (uint32_t)a2);
  }
  case SYS_FD_CTL:
    return fd_ctl((int32_t)a1, (uint32_t)a2, a3);
  case SYS_FD_STAT:
    if (!writable((void *)(uintptr_t)a2, sizeof(struct file_stat_info)))
      return -KERR_FAULT;
    return fd_stat((int32_t)a1, (struct file_stat_info *)(uintptr_t)a2);
  case SYS_FD_PATH:
    if (a3 > 4096 || !writable((void *)(uintptr_t)a2, a3))
      return -KERR_FAULT;
    return fd_path((int32_t)a1, (char *)(uintptr_t)a2, (uint32_t)a3);
  case SYS_FD_SEEK:
    return fd_seek((int32_t)a1, (int64_t)a2, (uint32_t)a3);
  case SYS_FD_TRUNCATE:
    return fd_truncate((int32_t)a1, a2);
  case SYS_CHDIR:
    if (!readable_string((const char *)(uintptr_t)a1))
      return -KERR_FAULT;
    return process_chdir((const char *)(uintptr_t)a1);
  case SYS_GETCWD:
  {
    uint32_t cwd_capacity =
        a2 > PROCESS_CWD_CAPACITY ? PROCESS_CWD_CAPACITY : (uint32_t)a2;
    if (!writable((void *)(uintptr_t)a1, cwd_capacity))
      return -KERR_FAULT;
    return process_getcwd((char *)(uintptr_t)a1, cwd_capacity);
  }
  case SYS_KILL:
    if ((int32_t)a1 <= 0)
      return -KERR_SRCH;
    return signal_send((uint32_t)a1, (int32_t)a2);
  case SYS_SIGACTION:
  {
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
  case SYS_SIGPROCMASK:
  {
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
  case SYS_SIGPENDING:
  {
    if (!writable((void *)(uintptr_t)a1, sizeof(uint64_t)))
      return -KERR_FAULT;
    uint64_t pending = 0;
    int32_t got = signal_pending_get(&pending);
    if (got >= 0)
      *(uint64_t *)(uintptr_t)a1 = pending;
    return got;
  }
  case SYS_SIGRETURN:
    return signal_return(r);
  case SYS_PAUSE:
    return signal_pause();
  case SYS_GETPPID:
    return process_parent_pid((uint32_t)process_current_pid());
  case SYS_FORK:
    return process_fork(r);
  case SYS_EXEC:
    if (!readable_string((const char *)(uintptr_t)a1) ||
        (a2 && !readable_string((const char *)(uintptr_t)a2)))
      return -1;
    if (a2 &&
        strlen((const char *)(uintptr_t)a2) >= PROCESS_COMMAND_LINE_CAPACITY)
      return -1;
    return process_spawn_module((const char *)(uintptr_t)a1,
                                (const char *)(uintptr_t)a2);
  case SYS_GET_COMMAND_LINE:
    if (a2 > 0x7FFFFFFFULL ||
        !writable((void *)(uintptr_t)a1, (uint32_t)a2))
      return -1;
    return process_command_line((char *)(uintptr_t)a1, (uint32_t)a2);
  case SYS_GET_PROCESS_NAME:
    if (a2 > 32 || !writable((void *)(uintptr_t)a1, (uint32_t)a2))
      return -1;
    return process_name((char *)(uintptr_t)a1, (uint32_t)a2);
  case SYS_ENV_GET:
    if (a3 > PROCESS_ENVIRONMENT_VALUE_CAPACITY)
      return -1;
    if (!readable_string((const char *)(uintptr_t)a1) ||
        !writable((void *)(uintptr_t)a2, (uint32_t)a3))
      return -1;
    return process_environment_get((const char *)(uintptr_t)a1,
                                   (char *)(uintptr_t)a2, (uint32_t)a3);
  case SYS_ENV_SET:
    if (!readable_string((const char *)(uintptr_t)a1) ||
        !readable_string((const char *)(uintptr_t)a2))
      return -1;
    return process_environment_set((const char *)(uintptr_t)a1,
                                   (const char *)(uintptr_t)a2);
  case SYS_ENV_UNSET:
    if (!readable_string((const char *)(uintptr_t)a1))
      return -1;
    return process_environment_unset((const char *)(uintptr_t)a1);
  case SYS_ENV_LIST:
  {
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
  case SYS_GET_MOUSE:
  {
    struct mouse_state *out = (struct mouse_state *)(uintptr_t)a1;
    if (!writable(out, sizeof(*out)))
      return -1;
    ps2_mouse_poll();
    usb_mouse_poll();
    *out = mouse_get_state();
    return 0;
  }
  case SYS_MOUSE_DEBUG_GET:
    return mouse_get_debug_overlay() ? 1 : 0;
  case SYS_MOUSE_DEBUG_SET:
    mouse_set_debug_overlay(a1 != 0);
    return 0;
  case SYS_FILE_OPEN:
  {
    if (!readable_string((const char *)(uintptr_t)a1))
      return -1;
    if (!filesystem_syscall_acquire())
      return -1;
    int32_t result = vfs_open((const char *)(uintptr_t)a1);
    if (result >= 0)
    {
      int32_t descriptor = process_fd_install(result);
      if (descriptor < 0)
        (void)vfs_close(result);
      result = descriptor;
    }
    filesystem_syscall_unlock();
    return result;
  }
  case SYS_FILE_READ:
  {
    if (!writable((void *)(uintptr_t)a2, (uint32_t)a3))
      return -1;
    int32_t descriptor = process_fd_resolve((int32_t)a1);
    if (descriptor < 0)
      return -1;
    if (!filesystem_syscall_acquire())
      return -1;
    int32_t result = vfs_read(descriptor, (void *)(uintptr_t)a2, (uint32_t)a3);
    filesystem_syscall_unlock();
    return result;
  }
  case SYS_FILE_SEEK:
  {
    int32_t descriptor = process_fd_resolve((int32_t)a1);
    if (descriptor < 0)
      return -1;
    if (!filesystem_syscall_acquire())
      return -1;
    int64_t result = vfs_seek(descriptor, (int64_t)a2, (uint32_t)a3);
    filesystem_syscall_unlock();
    return result;
  }
  case SYS_FILE_STAT:
  {
    if (!readable_string((const char *)(uintptr_t)a1) ||
        !writable((void *)(uintptr_t)a2, sizeof(struct file_stat_info)))
      return -1;
    if (!filesystem_syscall_acquire())
      return -1;
    int32_t result = vfs_stat((const char *)(uintptr_t)a1,
                              (struct file_stat_info *)(uintptr_t)a2);
    filesystem_syscall_unlock();
    return result;
  }
  case SYS_FILE_CLOSE:
  {
    if (!filesystem_syscall_acquire())
      return -1;
    int32_t result = process_fd_close((int32_t)a1);
    filesystem_syscall_unlock();
    return result;
  }
  case SYS_FILE_DELETE:
  {
    if (!readable_string((const char *)(uintptr_t)a1))
      return -1;
    if (!filesystem_syscall_acquire())
      return -1;
    int32_t result = vfs_delete((const char *)(uintptr_t)a1);
    filesystem_syscall_unlock();
    return result;
  }
  case SYS_FILE_RENAME:
  {
    if (!readable_string((const char *)(uintptr_t)a1) ||
        !readable_string((const char *)(uintptr_t)a2))
      return -1;
    if (!filesystem_syscall_acquire())
      return -1;
    int32_t result =
        vfs_rename((const char *)(uintptr_t)a1, (const char *)(uintptr_t)a2);
    filesystem_syscall_unlock();
    return result;
  }
  case SYS_FILE_MOVE:
  {
    if (!readable_string((const char *)(uintptr_t)a1) ||
        !readable_string((const char *)(uintptr_t)a2))
      return -1;
    if (!filesystem_syscall_acquire())
      return -1;
    int32_t result =
        vfs_move((const char *)(uintptr_t)a1, (const char *)(uintptr_t)a2);
    filesystem_syscall_unlock();
    return result;
  }
  case SYS_DIR_LIST:
  {
    if (!readable_string((const char *)(uintptr_t)a1) ||
        !writable((void *)(uintptr_t)a2,
                  (uint64_t)(uint32_t)a3 * sizeof(struct fs_directory_entry)))
      return -1;
    if (!filesystem_syscall_acquire())
      return -1;
    int32_t result =
        vfs_list((const char *)(uintptr_t)a1,
                 (struct fs_directory_entry *)(uintptr_t)a2, (uint32_t)a3);
    filesystem_syscall_unlock();
    return result;
  }
  case SYS_DIR_LIST_LONG:
  {
    if (!readable_string((const char *)(uintptr_t)a1) ||
        !writable((void *)(uintptr_t)a2,
                  (uint64_t)(uint32_t)a3 *
                      sizeof(struct fs_directory_entry_long)))
      return -1;
    if (!filesystem_syscall_acquire())
      return -1;
    int32_t result = vfs_list_long(
        (const char *)(uintptr_t)a1,
        (struct fs_directory_entry_long *)(uintptr_t)a2, (uint32_t)a3);
    filesystem_syscall_unlock();
    return result;
  }
  case SYS_IPC_SEND:
  {
    if (!a1 || !a2 || (uint32_t)a3 > IPC_MAX_PAYLOAD ||
        !readable((const void *)(uintptr_t)a2, (uint64_t)(uint32_t)a3))
      return -1;
    return ipc_send((uint32_t)a1, (const void *)(uintptr_t)a2,
                    (uint32_t)a3);
  }
  case SYS_IPC_RECV:
  {
    if (!a1 || !a2 ||
        !writable((void *)(uintptr_t)a1, (uint64_t)(uint32_t)a2))
      return -1;
    return ipc_recv((void *)(uintptr_t)a1, (uint32_t)a2, (uint32_t)a3);
  }
  case SYS_FILE_CREATE:
  {
    if (!readable_string((const char *)(uintptr_t)a1))
      return -1;
    if (!filesystem_syscall_acquire())
      return -1;
    int32_t result = vfs_create_file((const char *)(uintptr_t)a1);
    filesystem_syscall_unlock();
    return result;
  }
  case SYS_DIR_CREATE:
  {
    if (!readable_string((const char *)(uintptr_t)a1))
      return -1;
    if (!filesystem_syscall_acquire())
      return -1;
    int32_t result = vfs_create_directory((const char *)(uintptr_t)a1);
    filesystem_syscall_unlock();
    return result;
  }
  case SYS_DISK_LIST:
    if (!writable((void *)(uintptr_t)a1,
                  (uint64_t)(uint32_t)a2 * sizeof(struct storage_device_info)))
      return -1;
    return block_device_list((struct storage_device_info *)(uintptr_t)a1,
                             (uint32_t)a2);
  case SYS_STORAGE_CONTROLLERS:
    if (!writable((void *)(uintptr_t)a1,
                  (uint64_t)(uint32_t)a2 *
                      sizeof(struct storage_controller_info)))
      return -1;
    return storage_controller_list(
        (struct storage_controller_info *)(uintptr_t)a1, (uint32_t)a2);
  case SYS_FS_FORMAT:
  {
    if (!process_has_capability(PROCESS_CAP_STORAGE_ADMIN))
      return -1;
    if (!readable_string((const char *)(uintptr_t)a1) ||
        !readable_string((const char *)(uintptr_t)a2) ||
        (a3 && !readable_string((const char *)(uintptr_t)a3)))
      return -1;
    if (!filesystem_syscall_acquire())
      return -1;
    int32_t result = vfs_format_device((const char *)(uintptr_t)a1,
                                       (const char *)(uintptr_t)a2,
                                       (const char *)(uintptr_t)a3);
    filesystem_syscall_unlock();
    return result;
  }
  case SYS_FS_FORMAT_FORCE:
  {
    if (!process_has_capability(PROCESS_CAP_STORAGE_ADMIN))
      return -1;
    if (!readable_string((const char *)(uintptr_t)a1) ||
        !readable_string((const char *)(uintptr_t)a2))
      return -1;
    if (!filesystem_syscall_acquire())
      return -1;
    int32_t result = vfs_format_device_force((const char *)(uintptr_t)a1,
                                             (const char *)(uintptr_t)a2);
    filesystem_syscall_unlock();
    return result;
  }
  case SYS_INSTALL_DEVICE:
  {
    if (!process_has_capability(PROCESS_CAP_STORAGE_ADMIN))
      return -1;
    if (!readable_string((const char *)(uintptr_t)a1) ||
        !readable_string((const char *)(uintptr_t)a2))
      return -1;
    if (!filesystem_syscall_acquire())
      return -1;
    int32_t result = vfs_install_device((const char *)(uintptr_t)a1,
                                        (const char *)(uintptr_t)a2, 0);
    filesystem_syscall_unlock();
    return result;
  }
  case SYS_FILE_WRITE:
  {
    if (!readable_string((const char *)(uintptr_t)a1) ||
        !readable((const void *)(uintptr_t)a2, (uint32_t)a3))
      return -1;
    if (!filesystem_syscall_acquire())
      return -1;
    int32_t result = vfs_write_file((const char *)(uintptr_t)a1,
                                    (const void *)(uintptr_t)a2, (uint32_t)a3);
    filesystem_syscall_unlock();
    return result;
  }
  case SYS_FILE_APPEND:
  {
    if (!readable_string((const char *)(uintptr_t)a1) ||
        !readable((const void *)(uintptr_t)a2, (uint32_t)a3))
      return -1;
    if (!filesystem_syscall_acquire())
      return -1;
    int32_t result = vfs_append_file((const char *)(uintptr_t)a1,
                                     (const void *)(uintptr_t)a2, (uint32_t)a3);
    filesystem_syscall_unlock();
    return result;
  }
  case SYS_USB_RESCAN:
  {
    uint32_t count = block_device_rescan_usb();
    struct xhci_probe_stats xhci_stats;
    struct ehci_probe_stats ehci_stats;
    xhci_get_probe_stats(&xhci_stats);
    ehci_get_probe_stats(&ehci_stats);
    struct usb_scan_status *status = (struct usb_scan_status *)(uintptr_t)a1;
    if (status && !writable(status, sizeof(*status)))
      return -1;
    if (status)
    {
      status->xhci_controllers = xhci_stats.controllers;
      status->xhci_connected_ports = xhci_stats.connected_ports;
      status->xhci_addressed_devices = xhci_stats.addressed_devices;
      status->xhci_disks = xhci_stats.mass_storage_devices;
      status->xhci_hid_mice = xhci_stats.hid_mice;
      status->xhci_hid_interfaces = xhci_stats.hid_interfaces;
      status->xhci_hubs = xhci_stats.hubs;
      status->xhci_mouse_transfer_errors = xhci_stats.mouse_transfer_errors;
      status->xhci_failures = xhci_stats.failures;
      status->xhci_stage = xhci_stats.last_stage;
      status->xhci_error = xhci_stats.last_error;
      status->xhci_last_port = xhci_stats.last_port;
      status->xhci_portsc = xhci_stats.last_portsc;
      status->xhci_completion_code = xhci_stats.last_completion_code;
      status->xhci_max_ports = xhci_stats.max_ports;
      status->xhci_usb_status = xhci_stats.usb_status;
      status->xhci_scratchpad_count = xhci_stats.scratchpad_count;
      status->ehci_connected_ports = ehci_stats.connected_ports;
      status->ehci_high_speed_ports = ehci_stats.high_speed_ports;
      status->ehci_disks = ehci_stats.mass_storage_devices;
      status->ehci_failures = ehci_stats.failures;
      status->ehci_stage = ehci_stats.last_stage;
    }
    klogf(KLOG_INFO,
          "usb-rescan: xhci connected=%u addressed=%u disks=%u failures=%u "
          "stage=%u",
          xhci_stats.connected_ports, xhci_stats.addressed_devices,
          xhci_stats.mass_storage_devices, xhci_stats.failures,
          xhci_stats.last_stage);
    klogf(KLOG_INFO,
          "usb-rescan: xhci error=%u scratchpads=%u port=%u portsc=0x%x "
          "completion=%u usbsts=0x%x",
          xhci_stats.last_error, xhci_stats.scratchpad_count,
          xhci_stats.last_port, xhci_stats.last_portsc,
          xhci_stats.last_completion_code, xhci_stats.usb_status);
    klogf(KLOG_INFO,
          "usb-rescan: ehci connected=%u high-speed=%u disks=%u failures=%u "
          "stage=%u",
          ehci_stats.connected_ports, ehci_stats.high_speed_ports,
          ehci_stats.mass_storage_devices, ehci_stats.failures,
          ehci_stats.last_stage);
    if (count)
    {
      if (!filesystem_syscall_acquire())
      return -1;
      if (!vfs_is_root_mounted() && vfs_mount_root())
        wallclock_load_zone();
      filesystem_syscall_unlock();
    }
    return count;
  }
  case SYS_CPU_INFO:
  {
    struct cpu_monitor_info *info = (struct cpu_monitor_info *)(uintptr_t)a1;
    if (!writable(info, sizeof(*info)))
      return -1;
    memset(info, 0, sizeof(*info));
    strncpy(info->name, system_info_cpu_name(), sizeof(info->name) - 1);
    info->logical_processors = system_info_logical_processors();
    info->usage_percent = system_info_cpu_usage_percent();
    info->frequency_hz = system_info_tsc_frequency_hz();
    info->uptime_ms = system_info_uptime_ms();
    return 0;
  }
  case SYS_MEMORY_INFO:
  {
    struct memory_monitor_info *info =
        (struct memory_monitor_info *)(uintptr_t)a1;
    if (!writable(info, sizeof(*info)))
      return -1;
    info->total_bytes = system_info_total_ram_bytes();
    info->available_bytes = pmm_free_bytes();
    info->used_bytes = info->total_bytes > info->available_bytes
                           ? info->total_bytes - info->available_bytes
                           : 0;
    info->framebuffer_bytes = gop_get_framebuffer_size_bytes();
    return 0;
  }
  case SYS_CPU_CORE_INFO:
  {
    struct cpu_core_info *info = (struct cpu_core_info *)(uintptr_t)a1;
    if (!writable(info, sizeof(*info)))
      return -1;
    memset(info, 0, sizeof(*info));
    uint32_t registered = cpu_registered_count();
    if (registered > CPU_CORE_MAX_COUNT)
      registered = CPU_CORE_MAX_COUNT;
    info->count = registered;
    for (uint32_t i = 0; i < registered; i++)
    {
      info->cores[i].id = i;
      info->cores[i].online = cpu_is_online(i) ? 1u : 0u;
      uint64_t total = 0, idle = 0;
      (void)scheduler_cpu_ticks(i, &total, &idle);
      info->cores[i].total_ticks = total;
      info->cores[i].idle_ticks = idle;
    }
    return 0;
  }
  case SYS_PROCESS_LIST:
  {
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
  case SYS_DISK_STATS:
  {
    struct disk_monitor_info *info = (struct disk_monitor_info *)(uintptr_t)a1;
    if (!writable(info, sizeof(*info)))
      return -1;
    memset(info, 0, sizeof(*info));
    info->device_count = block_device_count();
    for (uint32_t index = 0; index < info->device_count; index++)
    {
      struct storage_device_info device;
      if (!block_device_get_info(index, &device))
        continue;
      if (device.operational)
        info->operational_count++;
      info->total_bytes += device.sector_count * device.sector_size;
    }
    return 0;
  }
  case SYS_THREAD_CREATE:
  {
    struct kernel_thread_args *request =
        (struct kernel_thread_args *)(uintptr_t)a1;
    if (!readable(request, sizeof(*request)))
      return -KERR_FAULT;
    struct kernel_thread_args copy = *request;
    return process_thread_create(copy.entry, copy.stack, copy.argument,
                                 copy.tls_base, copy.clear_tid);
  }
  case SYS_THREAD_EXIT:
    return process_thread_exit(a1, a2);
  case SYS_FUTEX:
  {
    struct process *owner = process_current();
    if (!owner)
      return -KERR_PERM;
    if (a1 == FUTEX_WAIT)
      return futex_wait(owner->address_space, a2, (uint32_t)a3,
                        (uint32_t)r->rsi);
    if (a1 == FUTEX_WAKE)
      return futex_wake(owner->address_space, a2,
                        a3 > INT32_MAX ? INT32_MAX : (int32_t)a3);
    return -KERR_INVAL;
  }
  case SYS_SET_FS_BASE:
    if (a1 >= 0x0000800000000000ULL)
      return -KERR_INVAL;
    scheduler_set_fs_base(a1);
    return 0;
  case SYS_GETTID:
    return scheduler_current_tid();
  case SYS_EXIT:
    process_exit_current((int32_t)a1);
  case SYS_WAIT:
    if (a2 && !writable((void *)(uintptr_t)a2, sizeof(int32_t)))
      return -1;
    return process_wait((uint32_t)a1, (int32_t *)(uintptr_t)a2, a3 != 0);
  case SYS_SLEEP:
    if (a1 > UINT32_MAX)
      return -1;
    scheduler_sleep((uint32_t)a1);
    return 0;
  case SYS_REBOOT:
    power_reboot();
    return 0;
  case SYS_SHUTDOWN:
    power_shutdown();
    return 0;
  case SYS_BATTERY_INFO:
  {
    struct battery_info *out = (struct battery_info *)(uintptr_t)a1;
    if (!writable(out, sizeof(*out)))
      return -1;
    return power_battery_get(out) ? 0 : -1;
  }
  case SYS_THERMAL_INFO:
  {
    struct thermal_info *out = (struct thermal_info *)(uintptr_t)a1;
    if (!writable(out, sizeof(*out)))
      return -1;
    return power_thermal_get(out) ? 0 : -1;
  }
  case SYS_AUDIO_GET_STATUS:
  {
    struct audio_status *out = (struct audio_status *)(uintptr_t)a1;
    if (!writable(out, sizeof(*out)))
      return -1;
    audio_get_status(out);
    return 0;
  }
  case SYS_AUDIO_GET_VOLUME:
    audio_update();
    return audio_get_volume();
  case SYS_AUDIO_SET_VOLUME:
    if (a1 > 100)
      return -1;
    audio_set_volume((uint8_t)a1);
    return 0;
  case SYS_AUDIO_IS_MUTED:
    audio_update();
    return audio_is_muted() ? 1 : 0;
  case SYS_AUDIO_SET_MUTED:
    audio_set_muted(a1 != 0);
    return 0;
  case SYS_AUDIO_ADJUST_VOLUME:
    audio_adjust_volume((int8_t)(int64_t)a1);
    return 0;
  case SYS_AUDIO_PLAY_TEST_SOUND:
    audio_play_test_sound();
    return 0;
  case SYS_AUDIO_PLAY_TONE:
  {
    uint32_t freq = (uint32_t)a1;
    uint32_t dur = (uint32_t)a2;
    if (freq < 30 || freq > 8000 || dur == 0 || dur > 5000)
      return -1;
    audio_play_tone(freq, dur);
    return 0;
  }
  case SYS_AUDIO_STOP_TONE:
    audio_stop_tone();
    return 0;
  case SYS_AUDIO_PCM_PUSH:
  {
    uint32_t frames = (uint32_t)a2;
    if (frames == 0)
    {
      audio_pcm_eos();
      return 0;
    }
    const int16_t *samples = (const int16_t *)(uintptr_t)a1;
    if (frames > 4096)
      return -1;
    if (!readable(samples, (uint64_t)frames * 2U))
      return -1;
    return audio_pcm_push(samples, frames);
  }
  case SYS_AUDIO_PCM_START:
    return audio_pcm_start() ? 0 : -1;
  case SYS_AUDIO_PCM_STOP:
    audio_pcm_stop_stream();
    return 0;
  case SYS_AUDIO_UPDATE:
    audio_update();
    return 0;
  case SYS_AUDIO_SELECT_OUTPUT_DEVICE:
    klogf(KLOG_INFO, "audio: syscall select output device index=%u",
          (uint32_t)a1);
    return audio_select_output_device((uint32_t)a1) ? 0 : -1;
  case SYS_SCHED_YIELD:
    scheduler_yield();
    return 0;
  case SYS_GETCHAR:
    return (uint8_t)keyboard_getc();
  case SYS_TRY_GETCHAR:
  {
    char character;
    return keyboard_try_getc(&character) ? (uint8_t)character : -1;
  }
  case SYS_TRY_GET_SPECIAL:
  {
    uint8_t key;
    return keyboard_try_get_special(&key) ? key : -1;
  }
  case SYS_KEY_EVENT_GET:
  {
    struct key_event *out = (struct key_event *)(uintptr_t)a1;
    if (!writable(out, sizeof(*out)))
      return -1;
    return keyboard_try_get_event(out) ? 1 : 0;
  }
  case SYS_INSTALL_START:
    if (!process_has_capability(PROCESS_CAP_STORAGE_ADMIN))
      return -10;
    if (!readable_string((const char *)(uintptr_t)a1) ||
        !readable_string((const char *)(uintptr_t)a2))
      return -11;
    if (install_job.state == 1)
      return -12;
    memset(&install_job, 0, sizeof(install_job));
    memset(&install_history, 0, sizeof(install_history));
    installer_set_accounts(0);
    strncpy(install_device, (const char *)(uintptr_t)a1,
            sizeof(install_device) - 1);
    strncpy(install_serial, (const char *)(uintptr_t)a2,
            sizeof(install_serial) - 1);
    install_job.state = 1;
    install_progress(1, "Starting installer worker");
    if (scheduler_create_thread(install_worker, 0, "installer-io",
                                INSTALL_WORKER_PRIORITY, 0) < 0)
    {
      install_job.state = 3;
      install_job.result = -1;
      install_progress(100, "Cannot start installer worker");
      return -1;
    }
    return 0;
  case SYS_INSTALL_START_EX:
  {
    if (!process_has_capability(PROCESS_CAP_STORAGE_ADMIN))
      return -10;
    const struct install_start_request *req =
        (const struct install_start_request *)(uintptr_t)a1;
    if (!readable(req, sizeof(*req)))
      return -11;
    if (install_job.state == 1)
      return -12;
    if (req->accounts.present && !install_accounts_valid(&req->accounts))
      return -14;
    memset(&install_job, 0, sizeof(install_job));
    memset(&install_history, 0, sizeof(install_history));
    installer_set_accounts(&req->accounts);
    strncpy(install_device, req->device, sizeof(install_device) - 1);
    strncpy(install_serial, req->serial, sizeof(install_serial) - 1);
    install_job.state = 1;
    install_progress(1, "Starting installer worker");
    if (scheduler_create_thread(install_worker, 0, "installer-io",
                                INSTALL_WORKER_PRIORITY, 0) < 0)
    {
      install_job.state = 3;
      install_job.result = -1;
      install_progress(100, "Cannot start installer worker");
      return -1;
    }
    return 0;
  }
  case SYS_INSTALL_STATUS:
    if (!writable((void *)(uintptr_t)a1, sizeof(install_job)))
      return -1;
    *(struct install_status *)(uintptr_t)a1 = install_job;
    return 0;
  case SYS_INSTALL_LOG:
    if (!writable((void *)(uintptr_t)a1, sizeof(install_history)))
      return -1;
    *(struct install_log *)(uintptr_t)a1 = install_history;
    return 0;
  case SYS_NET_PING:
  {
    const struct network_ping_request *user_request =
        (const struct network_ping_request *)(uintptr_t)a1;
    struct network_ping_result *user_result =
        (struct network_ping_result *)(uintptr_t)a2;
    if (!readable(user_request, sizeof(*user_request)) ||
        !writable(user_result, sizeof(*user_result)))
      return -1;
    struct network_ping_request request = *user_request;
    request.target[sizeof(request.target) - 1] = '\0';
    bool terminated = false;
    for (uint32_t index = 0; index < sizeof(request.target); index++)
    {
      if (!request.target[index])
      {
        terminated = true;
        break;
      }
    }
    if (!terminated || !request.target[0] || request.timeout_ms > 30000)
      return NET_PING_INVALID;
    struct net_ping_reply reply;
    enum net_ping_status status = net_ping_target(
        request.target, request.sequence, request.timeout_ms, &reply);
    if (status != NET_PING_OK)
      return status;
    user_result->address = reply.address;
    user_result->round_trip_ms = reply.round_trip_ms;
    user_result->sequence = reply.sequence;
    user_result->ttl = reply.ttl;
    user_result->reserved = 0;
    return NET_PING_OK;
  }
  case SYS_WIFI_SCAN:
  {
    if (!wifi_has_device())
      return -1;
    return wifi_trigger_scan() ? 0 : -1;
  }
  case SYS_WIFI_LIST:
  {
    struct wifi_network_info *buffer =
        (struct wifi_network_info *)(uintptr_t)a1;
    uint32_t capacity = (uint32_t)a2;
    if (capacity > WIFI_SCAN_MAX)
      return -1;
    if (capacity && !writable(buffer, capacity * sizeof(*buffer)))
      return -1;
    if (!capacity)
      return (int64_t)wifi_get_scan_results(0, 0);
    struct wifi_network temp[WIFI_SCAN_MAX];
    uint32_t count =
        wifi_get_scan_results((struct wifi_network *)temp, WIFI_SCAN_MAX);
    if (count > capacity)
      count = capacity;
    for (uint32_t i = 0; i < count; i++)
    {
      buffer[i] = *(struct wifi_network_info *)&temp[i];
    }
    return (int64_t)count;
  }
  case SYS_WIFI_CONNECT:
  {
    const struct wifi_connect_request *user_req =
        (const struct wifi_connect_request *)(uintptr_t)a1;
    if (!readable(user_req, sizeof(*user_req)))
      return -1;
    struct wifi_connect_request req = *user_req;
    req.ssid[sizeof(req.ssid) - 1] = '\0';
    req.password[sizeof(req.password) - 1] = '\0';
    bool ssid_term = false;
    for (uint32_t i = 0; i < sizeof(req.ssid); i++)
      if (!req.ssid[i])
      {
        ssid_term = true;
        break;
      }
    if (!ssid_term || !req.ssid[0])
      return -1;
    bool pass_term = false;
    for (uint32_t i = 0; i < sizeof(req.password); i++)
      if (!req.password[i])
      {
        pass_term = true;
        break;
      }
    if (!pass_term)
      return -1;
    return wifi_connect(req.ssid, req.password) ? 0 : -1;
  }
  case SYS_WIFI_DISCONNECT:
  {
    return wifi_disconnect() ? 0 : -1;
  }
  case SYS_WIFI_STATUS:
  {
    struct wifi_status_info *out = (struct wifi_status_info *)(uintptr_t)a1;
    if (!writable(out, sizeof(*out)))
      return -1;
    struct wifi_status st;
    if (!wifi_get_status(&st))
      return -1;
    memset(out, 0, sizeof(*out));
    out->state = st.state;
    out->connected = st.connected ? 1 : 0;
    memcpy(out->ssid, st.ssid, sizeof(out->ssid));
    memcpy(out->bssid, st.bssid, 6);
    out->rssi = st.rssi;
    out->channel = st.channel;
    out->security = st.security;
    out->last_error = st.last_error;
    out->ip_address = st.ip_address;
    memcpy(out->mac, st.mac, 6);
    memcpy(out->interface_name, st.interface_name, sizeof(out->interface_name));
    out->scan_count = st.scan_count;
    out->last_scan_ms = st.last_scan_ms;
    out->has_device = st.has_device ? 1 : 0;
    return 0;
  }
  case SYS_SAVE_KLOG:
  {
    const struct save_klog_request *user_req =
        (const struct save_klog_request *)(uintptr_t)a1;
    if (!readable(user_req, sizeof(*user_req)))
      return -1;
    struct save_klog_request req = *user_req;
    req.device[sizeof(req.device) - 1] = '\0';
    req.path[sizeof(req.path) - 1] = '\0';
    for (uint32_t i = 0; i < sizeof(req.device); i++)
      if (!req.device[i])
        break;
      else if (req.device[i] == '\n' || req.device[i] == '\r')
        req.device[i] = '\0';
    for (uint32_t i = 0; i < sizeof(req.path); i++)
      if (!req.path[i])
        break;
      else if (req.path[i] == '\n' || req.path[i] == '\r')
        req.path[i] = '\0';
    if (!req.device[0] || !req.path[0])
      return -1;
    return vfs_save_klog_to_device(req.device, req.path);
  }
  case SYS_GET_ROOT_DEVICE:
  {
    char *buffer = (char *)(uintptr_t)a1;
    uint32_t capacity = (uint32_t)a2;
    if (!writable(buffer, capacity))
      return -1;
    const char *root = vfs_root_device_name();
    if (!root || !root[0])
      return -1;
    uint32_t len = (uint32_t)strlen(root);
    if (len + 1 > capacity)
      return -1;
    memcpy(buffer, root, len + 1);
    return (int64_t)len;
  }
  case SYS_GET_FS_TYPE:
  {
    char *buffer = (char *)(uintptr_t)a1;
    uint32_t capacity = (uint32_t)a2;
    if (!writable(buffer, capacity))
      return -1;
    const char *name = vfs_fs_type_name(vfs_root_fs_type());
    uint32_t len = (uint32_t)strlen(name);
    if (len + 1 > capacity)
      return -1;
    memcpy(buffer, name, len + 1);
    return (int64_t)len;
  }
  case SYS_FS_FORMAT_CUSTOM:
  {
    const struct custom_format_request *user_req =
        (const struct custom_format_request *)(uintptr_t)a1;
    if (!readable(user_req, sizeof(*user_req)))
      return -1;
    struct custom_format_request req = *user_req;
    req.device[sizeof(req.device) - 1] = '\0';
    if (req.partition_count == 0 || req.partition_count > 4)
      return -1;
    if (!filesystem_syscall_acquire())
      return -1;
    int32_t result = installer_format_custom_device(
        req.device, req.partition_count, req.sizes_gb);
    filesystem_syscall_unlock();
    return result;
  }
  case SYS_FORMAT_DEVICE_EX:
  {
    if (!process_has_capability(PROCESS_CAP_STORAGE_ADMIN))
      return -1;
    const struct format_request *req =
        (const struct format_request *)(uintptr_t)a1;
    if (!readable(req, sizeof(*req)))
      return -1;
    struct format_request r = *req;
    r.device[sizeof(r.device) - 1] = '\0';
    r.serial[sizeof(r.serial) - 1] = '\0';
    r.erase[sizeof(r.erase) - 1] = '\0';
    if (!filesystem_syscall_acquire())
      return -1;
    int32_t res = vfs_format_device_ex(
        r.device, r.serial, r.erase,
        VFS_FS_EXT2);
    filesystem_syscall_unlock();
    return res;
  }
  case SYS_EXT2_STAT:
  {
    const char *path = (const char *)(uintptr_t)a1;
    struct ext2_stat_info *out = (struct ext2_stat_info *)(uintptr_t)a2;
    if (!readable_string(path) || !writable(out, sizeof(*out)))
      return -1;
    if (!filesystem_syscall_acquire())
      return -1;
    int32_t r = vfs_ext2_stat(path, out);
    filesystem_syscall_unlock();
    return r;
  }
  case SYS_EXT2_INODE:
  {
    uint32_t ino = (uint32_t)a1;
    struct ext2_stat_info *out = (struct ext2_stat_info *)(uintptr_t)a2;
    if (!writable(out, sizeof(*out)))
      return -1;
    if (!filesystem_syscall_acquire())
      return -1;
    int32_t r = vfs_ext2_inode(ino, out);
    filesystem_syscall_unlock();
    return r;
  }
  case SYS_EXT2_SUPER:
  {
    struct ext2_super_info *out = (struct ext2_super_info *)(uintptr_t)a1;
    if (!writable(out, sizeof(*out)))
      return -1;
    if (!filesystem_syscall_acquire())
      return -1;
    int32_t r = vfs_ext2_super(out);
    filesystem_syscall_unlock();
    return r;
  }
  case SYS_EXT2_BLOCKS:
  {
    const char *path = (const char *)(uintptr_t)a1;
    struct ext2_blocks_info *out = (struct ext2_blocks_info *)(uintptr_t)a2;
    if (!readable_string(path) || !writable(out, sizeof(*out)))
      return -1;
    if (!filesystem_syscall_acquire())
      return -1;
    int32_t r = vfs_ext2_blocks(path, out);
    filesystem_syscall_unlock();
    return r;
  }
  case SYS_TCP_SOCKET:
    return tcp_socket_owned(process_current()->pid);
  case SYS_TCP_CONNECT:
  case SYS_TCP_LISTEN:
  case SYS_TCP_ACCEPT:
  case SYS_TCP_SEND:
  case SYS_TCP_RECV:
  case SYS_TCP_CLOSE:
  case SYS_TCP_ABORT:
  case SYS_TCP_INFO:
  {
    int32_t handle = (int32_t)(uint32_t)a1;
    uint32_t timeout_ms = (uint32_t)(a1 >> 32);
    if (!tcp_is_owner(handle, process_current()->pid))
      return TCP_ERR_INVALID;
    switch (n)
    {
    case SYS_TCP_CONNECT:
      return tcp_connect(handle, (uint32_t)a2, (uint16_t)a3, timeout_ms);
    case SYS_TCP_LISTEN:
      return tcp_listen(handle, (uint16_t)a2, (uint8_t)a3);
    case SYS_TCP_ACCEPT:
      return tcp_accept(handle, timeout_ms);
    case SYS_TCP_SEND:
    {
      uint32_t length = (uint32_t)a3;
      if (!length)
        return 0;
      if (!readable((const void *)(uintptr_t)a2, length))
        return TCP_ERR_INVALID;
      return tcp_send(handle, (const void *)(uintptr_t)a2, length, timeout_ms);
    }
    case SYS_TCP_RECV:
    {
      uint32_t length = (uint32_t)a3;
      if (!length)
        return 0;
      if (!writable((void *)(uintptr_t)a2, length))
        return TCP_ERR_INVALID;
      return tcp_receive(handle, (void *)(uintptr_t)a2, length, timeout_ms);
    }
    case SYS_TCP_CLOSE:
      return tcp_close(handle);
    case SYS_TCP_ABORT:
      return tcp_abort(handle);
    default:
    {
      struct net_tcp_info *out = (struct net_tcp_info *)(uintptr_t)a2;
      struct tcp_socket_info info;
      if (!writable(out, sizeof(*out)))
        return TCP_ERR_INVALID;
      if (!tcp_get_info(handle, &info))
        return TCP_ERR_INVALID;
      out->state = (uint32_t)info.state;
      out->local_address = info.local_address;
      out->remote_address = info.remote_address;
      out->local_port = info.local_port;
      out->remote_port = info.remote_port;
      out->bytes_unread = info.bytes_unread;
      out->bytes_unsent = info.bytes_unsent;
      out->cwnd = info.cwnd;
      out->rto_ms = info.rto_ms;
      return 0;
    }
    }
  }
  case SYS_SOCKET:
  case SYS_CONNECT:
  case SYS_BIND:
  case SYS_LISTEN:
  case SYS_ACCEPT:
  case SYS_SOCK_SHUTDOWN:
  case SYS_SOCKNAME:
  case SYS_SETSOCKOPT:
  case SYS_GETSOCKOPT:
  case SYS_SOCK_SEND:
  case SYS_SOCK_RECV:
  case SYS_SOCKETPAIR:
  case SYS_SENDMSG:
  case SYS_RECVMSG:
  case SYS_SENDTO:
  case SYS_RECVFROM:
    return socket_syscall(n, a1, a2, a3, r->rsi, r->rdi);
  case SYS_DNS_RESOLVE:
    return -KERR_NOSYS;
  case SYS_NET_CONFIGURE:
  {
    if (process_current_uid() != 0 && !process_has_capability(PROCESS_CAP_NET_ADMIN))
      return -KERR_PERM;
    const struct net_if_config *user = (const struct net_if_config *)(uintptr_t)a1;
    if (!readable(user, sizeof(*user)))
      return -KERR_FAULT;
    struct net_if_config cfg = *user;
    cfg.name[sizeof(cfg.name) - 1] = '\0';
    struct net_device *dev = 0;
    for (uint32_t i = 0; i < net_device_count(); i++)
    {
      struct net_device *d = net_device_get(i);
      if (d && strcmp(d->name, cfg.name) == 0)
        dev = d;
    }
    if (!dev)
      return -KERR_NODEV;
    switch (cfg.op)
    {
    case NET_CFG_SET_IPV4:
      if (!cfg.address || !cfg.netmask)
        return -KERR_INVAL;
      return ipv4_configure(dev, cfg.address, cfg.netmask, cfg.gateway) ? 0 : -KERR_INVAL;
    case NET_CFG_CLEAR_IPV4:
      if (dev->loopback)
        return -KERR_PERM;
      return ipv4_unconfigure(dev) ? 0 : -KERR_INVAL;
    case NET_CFG_SET_DNS:
      return dns_set_server(dev, cfg.dns) ? 0 : -KERR_INVAL;
    case NET_CFG_DHCP_START:
      if (dev->loopback)
        return -KERR_INVAL;
      return dhcp_start(dev) ? 0 : -KERR_BUSY;
    case NET_CFG_DHCP_STOP:
      return dhcp_stop(dev) ? 0 : -KERR_INVAL;
    default:
      return -KERR_INVAL;
    }
  }
  case SYS_NET_IF_LIST:
  {
    struct net_if_info *buffer = (struct net_if_info *)(uintptr_t)a1;
    uint32_t capacity = (uint32_t)a2;
    if (capacity > NET_IF_MAX_COUNT)
      return -1;
    if (capacity && !writable(buffer, capacity * sizeof(*buffer)))
      return -1;
    uint32_t total = net_device_count();
    if (total > NET_IF_MAX_COUNT)
      total = NET_IF_MAX_COUNT;
    if (capacity == 0)
      return (int64_t)total;
    uint32_t copy = total < capacity ? total : capacity;
    for (uint32_t i = 0; i < copy; i++)
    {
      struct net_device *dev = net_device_get(i);
      struct net_if_info info;
      memset(&info, 0, sizeof(info));
      if (dev)
      {
        strncpy(info.name, dev->name, sizeof(info.name) - 1);
        memcpy(info.mac, dev->mac, 6);
        info.mtu = dev->mtu;
        info.link_up = dev->cached_link_up ? 1 : 0;
        struct ipv4_interface_config cfg;
        if (ipv4_get_config(dev, &cfg) && cfg.configured)
        {
          info.has_ip = 1;
          info.ip_address = cfg.address;
          info.netmask = cfg.netmask;
          info.gateway = cfg.gateway;
        }
        info.dns_server = dns_get_server(dev);
        info.dhcp_bound = dhcp_is_bound(dev) ? 1 : 0;
        info.rx_packets = dev->stats.rx_packets;
        info.tx_packets = dev->stats.tx_packets;
        info.rx_bytes = dev->stats.rx_bytes;
        info.tx_bytes = dev->stats.tx_bytes;
        info.rx_dropped = dev->stats.rx_dropped + dev->stats.rx_errors;
        info.tx_dropped = dev->stats.tx_dropped + dev->stats.tx_errors;
      }
      buffer[i] = info;
    }
    return (int64_t)copy;
  }
  default:
    serial_write_string("[SYSCALL] unknown n=");
    print_hex(n);
    serial_write_string("\n");
    return -1;
  }
}

static uint32_t parse_setting_u32(const char *text)
{
  uint32_t value = 0;
  while (*text >= '0' && *text <= '9')
  {
    uint32_t digit = (uint32_t)(*text - '0');
    if (value > (UINT32_MAX - digit) / 10U)
      return UINT32_MAX;
    value = value * 10U + digit;
    text++;
  }
  return value;
}

static void audio_restore_system_settings(void)
{
  int32_t descriptor = vfs_open("/config/settings.ini");
  if (descriptor < 0)
  {
    klog(KLOG_INFO, "audio: no saved system settings; detected defaults kept");
    return;
  }
  char buffer[256] = {0};
  int32_t amount = vfs_read(descriptor, buffer, sizeof(buffer) - 1);
  (void)vfs_close(descriptor);
  if (amount <= 0)
    return;
  buffer[amount] = '\0';
  struct audio_status detected = {0};
  audio_get_status(&detected);
  uint32_t volume = detected.volume;
  uint32_t device = detected.selected_output_device;
  bool muted = detected.muted != 0;
  for (char *line = buffer; *line;)
  {
    char *end = line;
    while (*end && *end != '\n' && *end != '\r')
      end++;
    char saved = *end;
    *end = '\0';
    if (strncmp(line, "volume=", 7) == 0)
      volume = parse_setting_u32(line + 7);
    else if (strncmp(line, "muted=", 6) == 0)
      muted = parse_setting_u32(line + 6) != 0;
    else if (strncmp(line, "device=", 7) == 0)
      device = parse_setting_u32(line + 7);
    if (!saved)
      break;
    line = end + 1;
    while (*line == '\n' || *line == '\r')
      line++;
  }
  if (volume > 100)
    volume = 100;
  if (device < detected.output_device_count)
    (void)audio_select_output_device(device);
  audio_set_volume((uint8_t)volume);
  audio_set_muted(muted);
  klogf(KLOG_OK, "audio: restored settings volume=%u mute=%u device=%u", volume,
        muted ? 1 : 0, device);
}

void syscall_init(void)
{
  static bool filesystem_checked;
  static bool audio_checked;
  klog(KLOG_DEBUG, "syscall: int 0x80 handler ready (DPL3)");
  if (!audio_checked)
  {
    audio_checked = true;
    bool screen_logging = klog_is_screen_enabled();
    klog_set_screen_enabled(false);
    audio_init();
    klog_set_screen_enabled(screen_logging);
    klog(KLOG_INFO, "audio: PC speaker backend ready");
  }
  if (!filesystem_checked)
  {
    filesystem_checked = true;
    boot_diag_checkpoint(BOOT_STAGE_SYSCALLS,
                         "storage: scanning PCI controllers");
    storage_probe_init();
    boot_diag_checkpoint(BOOT_STAGE_SYSCALLS,
                         "storage: initializing block transports");
    (void)block_device_init();
    struct ahci_probe_stats ahci_stats;
    struct xhci_probe_stats xhci_stats;
    struct ehci_probe_stats ehci_stats;
    ahci_get_probe_stats(&ahci_stats);
    xhci_get_probe_stats(&xhci_stats);
    ehci_get_probe_stats(&ehci_stats);
    klogf(KLOG_INFO, "pci: %u storage/USB controller(s) detected",
          storage_controller_count());
    klogf(KLOG_INFO, "storage: %u operational disk(s) detected",
          block_device_count());
    klogf(KLOG_INFO, "ahci: %u SATA disk(s) identified", ahci_device_count());
    klogf(KLOG_DEBUG,
          "ahci: controllers=%u ports=%u sata=%u identify_failures=%u",
          ahci_stats.controllers, ahci_stats.implemented_ports,
          ahci_stats.sata_ports, ahci_stats.identify_failures);
    klogf(KLOG_INFO,
          "xhci: controllers=%u connected=%u addressed=%u usb-storage=%u "
          "failures=%u stage=%u",
          xhci_stats.controllers, xhci_stats.connected_ports,
          xhci_stats.addressed_devices, xhci_stats.mass_storage_devices,
          xhci_stats.failures, xhci_stats.last_stage);
    klogf(KLOG_INFO,
          "ehci: controllers=%u connected=%u high-speed=%u usb-storage=%u "
          "failures=%u stage=%u",
          ehci_stats.controllers, ehci_stats.connected_ports,
          ehci_stats.high_speed_ports, ehci_stats.mass_storage_devices,
          ehci_stats.failures, ehci_stats.last_stage);
    klog(KLOG_DEBUG, "usb stages: 1=pci 2=running 3=port 4=addressed "
                     "5=bulk-endpoints 6=scsi 7=ready");
    if (vfs_mount_root())
    {
      klogf(KLOG_OK, "vfs: root mounted from %s", vfs_root_device_name());
      audio_restore_system_settings();
      wallclock_load_zone();
      {
        int32_t fd = vfs_open("/purec/install.cfg");
        if (fd >= 0)
        {
          char cfg_buf[512] = {0};
          int32_t r = vfs_read(fd, cfg_buf, sizeof(cfg_buf) - 1);
          (void)vfs_close(fd);
          if (r > 0)
          {
            klogf(KLOG_INFO, "config: /purec/install.cfg (%d bytes)", r);
            cfg_buf[r] = 0;
            char *p = cfg_buf;
            while (*p)
            {
              char line[128] = {0};
              uint32_t i = 0;
              while (*p && *p != '\n' && i < 127)
              {
                line[i++] = *p++;
              }
              if (*p == '\n')
                p++;
              if (line[0])
                klogf(KLOG_INFO, "  cfg: %s", line);
            }
          }
        }
        fd = vfs_open("/etc/hostname");
        if (fd >= 0)
        {
          char hn[64] = {0};
          int32_t r = vfs_read(fd, hn, sizeof(hn) - 1);
          (void)vfs_close(fd);
          if (r > 0)
          {
            hn[r] = 0;
            for (int i = 0; i < r; i++)
              if (hn[i] == '\n' || hn[i] == '\r')
              {
                hn[i] = 0;
                break;
              }
            klogf(KLOG_INFO, "config: hostname='%s'", hn);
          }
        }
        fd = vfs_open("/boot/loader.cfg");
        if (fd >= 0)
        {
          char lb[256] = {0};
          int32_t r = vfs_read(fd, lb, sizeof(lb) - 1);
          (void)vfs_close(fd);
          if (r > 0)
          {
            lb[r] = 0;
            klogf(KLOG_DEBUG, "config: /boot/loader.cfg %d bytes", r);
          }
        }
      }
    }
    else
    {
      klog(KLOG_WARN, "vfs: no ext2 root found (use 'install' or "
                      "'mkfs' to create)");
    }
    boot_diag_checkpoint(BOOT_STAGE_SYSCALLS,
                         "storage and filesystem probe complete");
  }
}
