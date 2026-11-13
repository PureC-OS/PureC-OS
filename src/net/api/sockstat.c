#include "sockstat.h"
#include "socket.h"
#include "../transport/tcp.h"
#include "../transport/udp.h"
#include "../transport/udp_endpoint.h"
#include "../../kernel/process/process.h"
#include "../../kernel/syscall/syscall.h"
#include "../../lib/string.h"

struct cursor {
  struct net_socket_entry *out;
  uint32_t capacity;
  uint32_t offset;
  uint32_t seen;
  uint32_t written;
};

static struct net_socket_entry *next_slot(struct cursor *c)
{
  if (c->written >= c->capacity)
    return 0;
  if (c->seen++ < c->offset)
    return 0;
  struct net_socket_entry *e = &c->out[c->written++];
  memset(e, 0, sizeof(*e));
  return e;
}

static void set_owner(struct net_socket_entry *e, int32_t sock_id)
{
  if (sock_id < 0)
    return;
  uint32_t pid = 0, uid = 0, fd = 0;
  if (process_socket_owner(sock_id, &pid, &uid, &fd, e->process,
                           sizeof(e->process)))
  {
    e->pid = pid;
    e->uid = uid;
    e->fd = fd;
  }
}

static void set_kernel(struct net_socket_entry *e)
{
  e->flags |= NET_SOCK_FLAG_KERNEL;
  strncpy(e->process, "kernel", sizeof(e->process) - 1);
}

static void list_kernel_udp(struct cursor *c)
{
  for (uint32_t i = 0; i < UDP_BINDING_SLOTS; i++)
  {
    uint16_t port;
    if (!udp_binding_port(i, &port))
      continue;
    struct net_socket_entry *e = next_slot(c);
    if (!e)
      continue;
    e->protocol = NET_SOCK_UDP;
    e->state = NET_SOCK_STATE_UNCONN;
    e->local_port = port;
    set_kernel(e);
  }
}

static void list_udp(struct cursor *c)
{
  for (int32_t i = 0; i < (int32_t)UDPE_SLOT_COUNT; i++)
  {
    struct udpe_info info;
    if (!udpe_info(i, &info))
      continue;
    struct net_socket_entry *e = next_slot(c);
    if (!e)
      continue;
    e->protocol = NET_SOCK_UDP;
    e->state = NET_SOCK_STATE_UNCONN;
    e->local_address = info.local_address;
    e->local_port = info.local_port;
    if (info.connected)
    {
      e->state = TCP_STATE_ESTABLISHED;
      e->remote_address = info.peer_address;
      e->remote_port = info.peer_port;
    }
    e->recv_queue = info.queued_bytes;
    if (info.reuse)
      e->flags |= NET_SOCK_FLAG_REUSE;
    if (info.broadcast)
      e->flags |= NET_SOCK_FLAG_BROADCAST;
    memcpy(e->device, info.device, sizeof(e->device) - 1);
    set_owner(e, sock_find(true, i, 0));
  }
}

static void list_tcp(struct cursor *c)
{
  uint32_t slots = tcp_slot_count();
  for (int32_t i = 0; i < (int32_t)slots; i++)
  {
    struct tcp_socket_info info;
    if (!tcp_get_info(i, &info))
      continue;
    struct net_socket_entry *e = next_slot(c);
    if (!e)
      continue;
    e->protocol = NET_SOCK_TCP;
    e->state = (uint8_t)info.state;
    e->local_address = info.local_address;
    e->local_port = info.local_port;
    e->remote_address = info.remote_address;
    e->remote_port = info.remote_port;
    e->recv_queue = info.bytes_unread;
    e->send_queue = info.bytes_unsent;
    if (info.device)
      strncpy(e->device, info.device, sizeof(e->device) - 1);
    if (info.orphan)
    {
      e->flags |= NET_SOCK_FLAG_ORPHAN;
      continue;
    }
    bool reuse = false;
    int32_t id = sock_find(false, i, &reuse);
    if (reuse)
      e->flags |= NET_SOCK_FLAG_REUSE;
    if (id >= 0)
      set_owner(e, id);
    else if (info.owner)
    {
      e->pid = info.owner;
      struct process *p = process_lookup_by_pid(info.owner);
      if (p)
      {
        e->uid = p->uid;
        strncpy(e->process, p->name, sizeof(e->process) - 1);
      }
    }
    else
      set_kernel(e);
  }
}

int32_t net_socket_list(struct net_socket_entry *out, uint32_t capacity,
                        uint32_t offset)
{
  struct cursor c = {out, capacity, offset, 0, 0};
  list_tcp(&c);
  list_udp(&c);
  list_kernel_udp(&c);
  return (int32_t)c.written;
}
