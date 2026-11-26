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

static int64_t unix_sock_listen(int32_t fd, int32_t id, uint64_t a2, uint64_t a3, uint64_t a4)
{
  bool nonblock = fd_nonblocking(fd);
  (void)nonblock;
  (void)a2;
  (void)a3;
  (void)a4;
  return unix_listen(id, (int32_t)a2);
}

static int64_t unix_sock_accept(int32_t fd, int32_t id, uint64_t a2, uint64_t a3, uint64_t a4)
{
  bool nonblock = fd_nonblocking(fd);
  (void)nonblock;
  (void)a2;
  (void)a3;
  (void)a4;
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

static int64_t unix_sock_shutdown(int32_t fd, int32_t id, uint64_t a2, uint64_t a3, uint64_t a4)
{
  bool nonblock = fd_nonblocking(fd);
  (void)nonblock;
  (void)a2;
  (void)a3;
  (void)a4;
  return unix_shutdown(id, (int32_t)a2);
}

static int64_t unix_sock_sockname(int32_t fd, int32_t id, uint64_t a2, uint64_t a3, uint64_t a4)
{
  bool nonblock = fd_nonblocking(fd);
  (void)nonblock;
  (void)a2;
  (void)a3;
  (void)a4;
  uint8_t name[UNIX_PATH_MAX + 1];
  int32_t length = unix_name(id, a4 != 0, name, sizeof(name));
  if (length < 0)
    return length;
  return unix_copy_address(a2, a3, name, (uint32_t)length);
}

static int64_t unix_sock_setsockopt(int32_t fd, int32_t id, uint64_t a2, uint64_t a3, uint64_t a4)
{
  bool nonblock = fd_nonblocking(fd);
  (void)nonblock;
  (void)a2;
  (void)a3;
  (void)a4;
  return 0;
}

static int64_t unix_sock_getsockopt(int32_t fd, int32_t id, uint64_t a2, uint64_t a3, uint64_t a4)
{
  bool nonblock = fd_nonblocking(fd);
  (void)nonblock;
  (void)a2;
  (void)a3;
  (void)a4;
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

static int64_t unix_bind_socket(int32_t fd, int32_t id, uint64_t a2, uint64_t a3, uint64_t a4)
{
  (void)fd;
  (void)a4;
  uint8_t name[UNIX_PATH_MAX + 1];
  uint32_t length = 0;
  int64_t status = unix_read_address(a2, a3, name, &length);
  if (status < 0)
    return status;
  return unix_bind(id, name, length);
}

static int64_t unix_connect_socket(int32_t fd, int32_t id, uint64_t a2, uint64_t a3, uint64_t a4)
{
  (void)a4;
  bool nonblock = fd_nonblocking(fd);
  uint8_t name[UNIX_PATH_MAX + 1];
  uint32_t length = 0;
  int64_t status = unix_read_address(a2, a3, name, &length);
  if (status < 0)
    return status;
  return unix_connect(id, name, length, nonblock);
}

static int64_t unix_send_socket(int32_t fd, int32_t id, uint64_t a2, uint64_t a3, uint64_t a4)
{
  bool nonblock = fd_nonblocking(fd);
  if (a3 > 0x7FFFFFFFULL)
    return -KERR_INVAL;
  struct unix_iov iov = {a2, a3};
  if (!readable((const void *)(uintptr_t)a2, a3))
    return -KERR_FAULT;
  return unix_send(id, &iov, 1, 0, 0, nonblock, (uint32_t)a4);
}

static int64_t unix_recv_socket(int32_t fd, int32_t id, uint64_t a2, uint64_t a3, uint64_t a4)
{
  bool nonblock = fd_nonblocking(fd);
  if (a3 > 0x7FFFFFFFULL)
    return -KERR_INVAL;
  struct unix_iov iov = {a2, a3};
  if (!writable((void *)(uintptr_t)a2, a3))
    return -KERR_FAULT;
  return unix_recv(id, &iov, 1, 0, 0, 0, 0, nonblock, (uint32_t)a4);
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

static bool tcp_denied(int32_t handle)
{
  return !tcp_is_owner(handle, process_current()->pid);
}

long sys_socket(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a4;
  (void)a5;
  (void)a6;
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

long sys_socketpair(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a5;
  (void)a6;
  return socketpair_syscall(a1, a2, a3, a4);
}

long sys_bind(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a5;
  (void)a6;
  int32_t fd = (int32_t)a1;
  int32_t unix_id = unix_of_fd(fd);
  if (unix_id >= 0)
    return unix_bind_socket(fd, unix_id, a2, a3, a4);
  int32_t id = socket_of_fd(fd);
  if (id < 0)
    return id;
  struct sock_addr_in address;
  int64_t copied = socket_read_address(a2, a3, &address);
  if (copied < 0)
    return copied;
  return sock_bind(id, &address);
}

long sys_connect(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a5;
  (void)a6;
  int32_t fd = (int32_t)a1;
  int32_t unix_id = unix_of_fd(fd);
  if (unix_id >= 0)
    return unix_connect_socket(fd, unix_id, a2, a3, a4);
  int32_t id = socket_of_fd(fd);
  if (id < 0)
    return id;
  struct sock_addr_in address;
  int64_t copied = socket_read_address(a2, a3, &address);
  if (copied < 0)
    return copied;
  return sock_connect(id, &address, fd_nonblocking(fd));
}

long sys_listen(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a5;
  (void)a6;
  int32_t fd = (int32_t)a1;
  int32_t unix_id = unix_of_fd(fd);
  if (unix_id >= 0)
    return unix_sock_listen(fd, unix_id, a2, a3, a4);
  int32_t id = socket_of_fd(fd);
  return id < 0 ? id : sock_listen(id, (int32_t)a2);
}

long sys_accept(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a5;
  (void)a6;
  int32_t fd = (int32_t)a1;
  int32_t unix_id = unix_of_fd(fd);
  if (unix_id >= 0)
    return unix_sock_accept(fd, unix_id, a2, a3, a4);
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

long sys_sock_shutdown(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a5;
  (void)a6;
  int32_t fd = (int32_t)a1;
  int32_t unix_id = unix_of_fd(fd);
  if (unix_id >= 0)
    return unix_sock_shutdown(fd, unix_id, a2, a3, a4);
  int32_t id = socket_of_fd(fd);
  return id < 0 ? id : sock_shutdown(id, (int32_t)a2);
}

long sys_sockname(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a5;
  (void)a6;
  int32_t fd = (int32_t)a1;
  int32_t unix_id = unix_of_fd(fd);
  if (unix_id >= 0)
    return unix_sock_sockname(fd, unix_id, a2, a3, a4);
  int32_t id = socket_of_fd(fd);
  if (id < 0)
    return id;
  struct sock_addr_in address;
  int32_t result = sock_name(id, a4 != 0, &address);
  if (result < 0)
    return result;
  return socket_copy_address(a2, a3, &address);
}

long sys_setsockopt(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a5;
  (void)a6;
  int32_t fd = (int32_t)a1;
  int32_t unix_id = unix_of_fd(fd);
  if (unix_id >= 0)
    return unix_sock_setsockopt(fd, unix_id, a2, a3, a4);
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

long sys_getsockopt(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a5;
  (void)a6;
  int32_t fd = (int32_t)a1;
  int32_t unix_id = unix_of_fd(fd);
  if (unix_id >= 0)
    return unix_sock_getsockopt(fd, unix_id, a2, a3, a4);
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

long sys_sendto(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a6;
  int32_t fd = (int32_t)a1;
  int32_t unix_id = unix_of_fd(fd);
  if (unix_id >= 0)
    return unix_send_socket(fd, unix_id, a2, a3, a4);
  int32_t id = socket_of_fd(fd);
  if (id < 0)
    return id;
  if (a3 > 0x7FFFFFFFULL)
    return -KERR_INVAL;
  struct sock_addr_in address;
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

long sys_recvfrom(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a6;
  int32_t fd = (int32_t)a1;
  int32_t unix_id = unix_of_fd(fd);
  if (unix_id >= 0)
    return unix_recv_socket(fd, unix_id, a2, a3, a4);
  int32_t id = socket_of_fd(fd);
  if (id < 0)
    return id;
  if (a3 > 0x7FFFFFFFULL)
    return -KERR_INVAL;
  struct sock_addr_in address;
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

long sys_sock_send(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a5;
  (void)a6;
  int32_t fd = (int32_t)a1;
  int32_t unix_id = unix_of_fd(fd);
  if (unix_id >= 0)
    return unix_send_socket(fd, unix_id, a2, a3, a4);
  int32_t id = socket_of_fd(fd);
  if (id < 0)
    return id;
  if (a3 > 0x7FFFFFFFULL)
    return -KERR_INVAL;
  if (!readable((const void *)(uintptr_t)a2, a3))
    return -KERR_FAULT;
  return sock_write(id, (const void *)(uintptr_t)a2, (uint32_t)a3,
                    fd_nonblocking(fd), (uint32_t)a4);
}

long sys_sock_recv(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a5;
  (void)a6;
  int32_t fd = (int32_t)a1;
  int32_t unix_id = unix_of_fd(fd);
  if (unix_id >= 0)
    return unix_recv_socket(fd, unix_id, a2, a3, a4);
  int32_t id = socket_of_fd(fd);
  if (id < 0)
    return id;
  if (a3 > 0x7FFFFFFFULL)
    return -KERR_INVAL;
  if (!writable((void *)(uintptr_t)a2, a3))
    return -KERR_FAULT;
  return sock_read(id, (void *)(uintptr_t)a2, (uint32_t)a3,
                   fd_nonblocking(fd), (uint32_t)a4);
}

long sys_sendmsg(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a4;
  (void)a5;
  (void)a6;
  return sendmsg_syscall((int32_t)a1, a2, (uint32_t)a3);
}

long sys_recvmsg(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a4;
  (void)a5;
  (void)a6;
  return recvmsg_syscall((int32_t)a1, a2, (uint32_t)a3);
}

long sys_tcp_socket(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a1;
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  return tcp_socket_owned(process_current()->pid);
}

long sys_tcp_connect(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a4;
  (void)a5;
  (void)a6;
  int32_t handle = (int32_t)(uint32_t)a1;
  uint32_t timeout_ms = (uint32_t)(a1 >> 32);
  if (tcp_denied(handle))
    return TCP_ERR_INVALID;
  return tcp_connect(handle, (uint32_t)a2, (uint16_t)a3, timeout_ms);
}

long sys_tcp_listen(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a4;
  (void)a5;
  (void)a6;
  int32_t handle = (int32_t)(uint32_t)a1;
  if (tcp_denied(handle))
    return TCP_ERR_INVALID;
  return tcp_listen(handle, (uint16_t)a2, (uint8_t)a3);
}

long sys_tcp_accept(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  int32_t handle = (int32_t)(uint32_t)a1;
  uint32_t timeout_ms = (uint32_t)(a1 >> 32);
  if (tcp_denied(handle))
    return TCP_ERR_INVALID;
  return tcp_accept(handle, timeout_ms);
}

long sys_tcp_send(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a4;
  (void)a5;
  (void)a6;
  int32_t handle = (int32_t)(uint32_t)a1;
  uint32_t timeout_ms = (uint32_t)(a1 >> 32);
  if (tcp_denied(handle))
    return TCP_ERR_INVALID;
  uint32_t length = (uint32_t)a3;
  if (!length)
    return 0;
  if (!readable((const void *)(uintptr_t)a2, length))
    return TCP_ERR_INVALID;
  return tcp_send(handle, (const void *)(uintptr_t)a2, length, timeout_ms);
}

long sys_tcp_recv(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a4;
  (void)a5;
  (void)a6;
  int32_t handle = (int32_t)(uint32_t)a1;
  uint32_t timeout_ms = (uint32_t)(a1 >> 32);
  if (tcp_denied(handle))
    return TCP_ERR_INVALID;
  uint32_t length = (uint32_t)a3;
  if (!length)
    return 0;
  if (!writable((void *)(uintptr_t)a2, length))
    return TCP_ERR_INVALID;
  return tcp_receive(handle, (void *)(uintptr_t)a2, length, timeout_ms);
}

long sys_tcp_close(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  int32_t handle = (int32_t)(uint32_t)a1;
  if (tcp_denied(handle))
    return TCP_ERR_INVALID;
  return tcp_close(handle);
}

long sys_tcp_abort(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  int32_t handle = (int32_t)(uint32_t)a1;
  if (tcp_denied(handle))
    return TCP_ERR_INVALID;
  return tcp_abort(handle);
}

long sys_tcp_info(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  int32_t handle = (int32_t)(uint32_t)a1;
  if (tcp_denied(handle))
    return TCP_ERR_INVALID;
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

long sys_dns_resolve(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a1;
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  return -KERR_NOSYS;
}

long sys_net_ping(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
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

long sys_net_if_list(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
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

long sys_net_configure(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
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

long sys_net_sockets(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a4;
  (void)a5;
  (void)a6;
  struct net_socket_entry *out = (struct net_socket_entry *)(uintptr_t)a1;
  uint32_t capacity = (uint32_t)a2;
  if (capacity == 0 || capacity > NET_SOCKETS_MAX)
    return -KERR_INVAL;
  if (!writable(out, capacity * sizeof(*out)))
    return -KERR_FAULT;
  int32_t count = net_socket_list(out, capacity, (uint32_t)a3);
  uint32_t self_uid = process_current_uid();
  if (self_uid != 0)
    for (int32_t i = 0; i < count; i++)
      if (!(out[i].flags & NET_SOCK_FLAG_KERNEL) && out[i].pid && out[i].uid != self_uid)
      {
        out[i].pid = 0;
        out[i].fd = 0;
        memset(out[i].process, 0, sizeof(out[i].process));
      }
  return count;
}
