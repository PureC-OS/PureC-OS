#include "udp_endpoint.h"
#include "../core/net_device.h"
#include "../network/ipv4.h"
#include "../../drivers/interrupts/timer.h"
#include "../../kernel/process/scheduler.h"
#include "../../kernel/process/signal.h"
#include "../../kernel/sync/spinlock.h"
#include "../../kernel/syscall/syscall.h"
#include "../../lib/string.h"
#include "../../mm/pmm.h"

#define UDPE_MAX 128
#define UDPE_SLOTS 16
#define UDPE_EPHEMERAL_FIRST 49152U
#define UDPE_EPHEMERAL_LAST 65535U
#define UDPE_SEND_TIMEOUT_MS 1500U

struct udpe_slot {
  uint32_t source;
  uint16_t port;
  uint16_t length;
  uint8_t data[UDP_PAYLOAD_MAX_LENGTH];
};

struct udpe {
  bool used;
  bool bound;
  bool connected;
  bool broadcast;
  bool reuse;
  uint32_t local_address;
  uint16_t local_port;
  uint32_t peer_address;
  uint16_t peer_port;
  struct net_device *device;
  struct udpe_slot *ring;
  uint64_t ring_phys;
  uint32_t head;
  uint32_t tail;
  uint32_t dropped;
};

#define UDPE_RING_PAGES                                                        \
  ((sizeof(struct udpe_slot) * UDPE_SLOTS + PMM_PAGE_SIZE - 1) / PMM_PAGE_SIZE)

static spinlock_t udpe_lock = SPINLOCK_INIT;
static struct udpe table[UDPE_MAX];
static uint16_t next_ephemeral = UDPE_EPHEMERAL_FIRST;

static struct udpe *get(int32_t id)
{
  if (id < 0 || id >= UDPE_MAX || !table[id].used)
    return 0;
  return &table[id];
}

int32_t udpe_create(void)
{
  uint64_t phys = pmm_allocate_contiguous(UDPE_RING_PAGES);
  if (!phys)
    return -KERR_NOBUFS;
  uint64_t flags = spin_lock_irqsave(&udpe_lock);
  for (int32_t i = 0; i < UDPE_MAX; i++)
  {
    if (table[i].used)
      continue;
    memset(&table[i], 0, sizeof(table[i]));
    table[i].used = true;
    table[i].ring_phys = phys;
    table[i].ring = (struct udpe_slot *)pmm_physical_to_virtual(phys);
    spin_unlock_irqrestore(&udpe_lock, flags);
    return i;
  }
  spin_unlock_irqrestore(&udpe_lock, flags);
  pmm_free_contiguous(phys, UDPE_RING_PAGES);
  return -KERR_NFILE;
}

void udpe_release(int32_t id)
{
  uint64_t phys = 0;
  uint64_t flags = spin_lock_irqsave(&udpe_lock);
  struct udpe *e = get(id);
  if (e)
  {
    phys = e->ring_phys;
    memset(e, 0, sizeof(*e));
  }
  spin_unlock_irqrestore(&udpe_lock, flags);
  if (phys)
    pmm_free_contiguous(phys, UDPE_RING_PAGES);
}

static bool port_taken_locked(uint16_t port, uint32_t address, bool reuse,
                              int32_t self)
{
  for (int32_t i = 0; i < UDPE_MAX; i++)
  {
    if (i == self || !table[i].used || !table[i].bound ||
        table[i].local_port != port)
      continue;
    if (reuse && table[i].reuse)
      continue;
    if (table[i].local_address && address &&
        table[i].local_address != address)
      continue;
    return true;
  }
  return false;
}

bool udpe_port_in_use(uint16_t port)
{
  uint64_t flags = spin_lock_irqsave(&udpe_lock);
  bool taken = port_taken_locked(port, 0, false, -1);
  spin_unlock_irqrestore(&udpe_lock, flags);
  return taken;
}

static int32_t bind_locked(struct udpe *e, int32_t id, uint32_t address,
                           uint16_t port)
{
  if (e->bound)
    return -KERR_INVAL;
  if (!port)
  {
    for (uint32_t tries = 0; tries <= UDPE_EPHEMERAL_LAST - UDPE_EPHEMERAL_FIRST;
         tries++)
    {
      uint16_t candidate = next_ephemeral;
      next_ephemeral = candidate >= UDPE_EPHEMERAL_LAST
                           ? (uint16_t)UDPE_EPHEMERAL_FIRST
                           : (uint16_t)(candidate + 1);
      if (!port_taken_locked(candidate, address, false, id) &&
          !udp_port_bound(candidate))
      {
        port = candidate;
        break;
      }
    }
    if (!port)
      return -KERR_ADDRINUSE;
  }
  else if (port_taken_locked(port, address, e->reuse, id) ||
           udp_port_bound(port))
    return -KERR_ADDRINUSE;
  e->local_address = address;
  e->local_port = port;
  e->bound = true;
  return 0;
}

int32_t udpe_bind(int32_t id, uint32_t address, uint16_t port, bool reuse)
{
  uint64_t flags = spin_lock_irqsave(&udpe_lock);
  struct udpe *e = get(id);
  int32_t status = -KERR_BADF;
  if (e)
  {
    e->reuse = reuse;
    status = bind_locked(e, id, address, port);
  }
  spin_unlock_irqrestore(&udpe_lock, flags);
  return status;
}

int32_t udpe_connect(int32_t id, uint32_t address, uint16_t port)
{
  uint64_t flags = spin_lock_irqsave(&udpe_lock);
  struct udpe *e = get(id);
  int32_t status = -KERR_BADF;
  if (e)
  {
    status = 0;
    if (!address && !port)
      e->connected = false;
    else
    {
      if (!e->bound)
        status = bind_locked(e, id, 0, 0);
      if (status == 0)
      {
        e->peer_address = address;
        e->peer_port = port;
        e->connected = true;
      }
    }
  }
  spin_unlock_irqrestore(&udpe_lock, flags);
  return status;
}

int32_t udpe_set_broadcast(int32_t id, bool enabled)
{
  uint64_t flags = spin_lock_irqsave(&udpe_lock);
  struct udpe *e = get(id);
  if (e)
    e->broadcast = enabled;
  spin_unlock_irqrestore(&udpe_lock, flags);
  return e ? 0 : -KERR_BADF;
}

int32_t udpe_bind_device(int32_t id, const char *name)
{
  struct net_device *found = 0;
  if (name && name[0])
  {
    for (uint32_t i = 0; i < net_device_count(); i++)
    {
      struct net_device *d = net_device_get(i);
      if (d && strcmp(d->name, name) == 0)
        found = d;
    }
    if (!found)
      return -KERR_NODEV;
  }
  uint64_t flags = spin_lock_irqsave(&udpe_lock);
  struct udpe *e = get(id);
  if (e)
    e->device = found;
  spin_unlock_irqrestore(&udpe_lock, flags);
  return e ? 0 : -KERR_BADF;
}

bool udpe_info(int32_t id, struct udpe_info *out)
{
  uint64_t flags = spin_lock_irqsave(&udpe_lock);
  struct udpe *e = get(id);
  if (e)
  {
    memset(out, 0, sizeof(*out));
    out->bound = e->bound;
    out->connected = e->connected;
    out->broadcast = e->broadcast;
    out->reuse = e->reuse;
    out->local_address = e->local_address;
    out->local_port = e->local_port;
    out->peer_address = e->peer_address;
    out->peer_port = e->peer_port;
    out->dropped = e->dropped;
    for (uint32_t i = e->tail; i != e->head; i = (i + 1) % UDPE_SLOTS)
      out->queued_bytes += e->ring[i].length;
    if (e->device)
      strncpy(out->device, e->device->name, sizeof(out->device) - 1);
  }
  spin_unlock_irqrestore(&udpe_lock, flags);
  return e != 0;
}

int32_t udpe_name(int32_t id, bool peer, uint32_t *address, uint16_t *port)
{
  uint64_t flags = spin_lock_irqsave(&udpe_lock);
  struct udpe *e = get(id);
  int32_t status = -KERR_BADF;
  if (e)
  {
    if (peer)
    {
      status = e->connected ? 0 : -KERR_NOTCONN;
      *address = e->peer_address;
      *port = e->peer_port;
    }
    else
    {
      status = 0;
      *address = e->local_address;
      *port = e->local_port;
    }
  }
  spin_unlock_irqrestore(&udpe_lock, flags);
  return status;
}

static bool loopback(uint32_t address) { return (address >> 24) == 127; }

static bool enqueue_locked(struct udpe *e, uint32_t source, uint16_t port,
                           const uint8_t *data, uint16_t length)
{
  uint32_t next = (e->head + 1) % UDPE_SLOTS;
  if (next == e->tail)
  {
    e->dropped++;
    return false;
  }
  struct udpe_slot *slot = &e->ring[e->head];
  slot->source = source;
  slot->port = port;
  slot->length = length;
  if (length)
    memcpy(slot->data, data, length);
  e->head = next;
  return true;
}

static bool accepts(const struct udpe *e, const struct udp_datagram *d)
{
  if (!e->used || !e->bound || e->local_port != d->destination_port)
    return false;
  if (e->device && d->device && e->device != d->device)
    return false;
  if (e->local_address && e->local_address != d->destination_address &&
      d->destination_address != IPV4_BROADCAST &&
      !(loopback(e->local_address) && loopback(d->destination_address)))
    return false;
  if (e->connected &&
      (e->peer_address != d->source_address || e->peer_port != d->source_port))
    return false;
  return true;
}

bool udpe_deliver(const struct udp_datagram *datagram)
{
  bool delivered = false;
  bool multicast = datagram->destination_address == IPV4_BROADCAST;
  uint64_t flags = spin_lock_irqsave(&udpe_lock);
  for (int32_t i = 0; i < UDPE_MAX; i++)
  {
    if (!accepts(&table[i], datagram))
      continue;
    enqueue_locked(&table[i], datagram->source_address, datagram->source_port,
                   datagram->payload, datagram->payload_length);
    delivered = true;
    if (!multicast && !table[i].reuse)
      break;
  }
  spin_unlock_irqrestore(&udpe_lock, flags);
  return delivered;
}

static struct net_device *route(struct net_device *bound, uint32_t destination,
                                uint32_t *source)
{
  struct ipv4_interface_config config;
  if (bound)
  {
    *source = ipv4_get_config(bound, &config) && config.configured
                  ? config.address
                  : 0;
    return bound;
  }
  struct net_device *fallback = 0;
  for (uint32_t i = 0; i < net_device_count(); i++)
  {
    struct net_device *d = net_device_get(i);
    if (!d || !ipv4_get_config(d, &config) || !config.configured)
      continue;
    if ((destination & config.netmask) == (config.address & config.netmask))
    {
      *source = config.address;
      return d;
    }
    if (!fallback && config.gateway)
      fallback = d;
  }
  struct net_device *d = fallback ? fallback : net_device_default();
  *source = d && ipv4_get_config(d, &config) && config.configured
                ? config.address
                : 0;
  return d;
}

int32_t udpe_sendto(int32_t id, const void *buffer, uint32_t length,
                    bool has_target, uint32_t address, uint16_t port)
{
  if (length > UDP_PAYLOAD_MAX_LENGTH)
    return -KERR_MSGSIZE;
  uint64_t flags = spin_lock_irqsave(&udpe_lock);
  struct udpe *e = get(id);
  if (!e)
  {
    spin_unlock_irqrestore(&udpe_lock, flags);
    return -KERR_BADF;
  }
  if (!has_target)
  {
    if (!e->connected)
    {
      spin_unlock_irqrestore(&udpe_lock, flags);
      return -KERR_DESTADDRREQ;
    }
    address = e->peer_address;
    port = e->peer_port;
  }
  if (!port)
  {
    spin_unlock_irqrestore(&udpe_lock, flags);
    return -KERR_INVAL;
  }
  if (address == IPV4_BROADCAST && !e->broadcast)
  {
    spin_unlock_irqrestore(&udpe_lock, flags);
    return -KERR_ACCES;
  }
  if (!e->bound)
  {
    int32_t status = bind_locked(e, id, 0, 0);
    if (status < 0)
    {
      spin_unlock_irqrestore(&udpe_lock, flags);
      return status;
    }
  }
  uint16_t source_port = e->local_port;
  uint32_t bound_address = e->local_address;
  struct net_device *bound_device = e->device;
  spin_unlock_irqrestore(&udpe_lock, flags);

  if (loopback(address))
  {
    struct udp_datagram d = {
        .device = 0,
        .source_address = UDPE_LOOPBACK_ADDRESS,
        .destination_address = address,
        .source_port = source_port,
        .destination_port = port,
        .payload = (const uint8_t *)buffer,
        .payload_length = (uint16_t)length};
    udpe_deliver(&d);
    return (int32_t)length;
  }

  uint32_t source = 0;
  struct net_device *device = route(bound_device, address, &source);
  if (!device)
    return -KERR_NETUNREACH;
  if (bound_address && !loopback(bound_address))
    source = bound_address;
  if (!source && address != IPV4_BROADCAST)
    return -KERR_NETUNREACH;
  uint64_t deadline = timer_ticks() + UDPE_SEND_TIMEOUT_MS;
  for (;;)
  {
    enum ipv4_send_result result =
        udp_send_from(device, source, address, source_port, port,
                      (const uint8_t *)buffer, (uint16_t)length);
    if (result == IPV4_SEND_OK)
      return (int32_t)length;
    if (timer_ticks() >= deadline)
      return result == IPV4_SEND_WAITING_ARP ? -KERR_HOSTUNREACH : -KERR_IO;
    if (signal_pending_current())
      return -KERR_INTR;
    scheduler_sleep(result == IPV4_SEND_WAITING_ARP ? 10 : 1);
  }
}

int32_t udpe_recvfrom(int32_t id, void *buffer, uint32_t length, bool nonblock,
                      uint32_t timeout_ms, bool peek, uint32_t *address,
                      uint16_t *port)
{
  uint8_t scratch[UDP_PAYLOAD_MAX_LENGTH];
  uint64_t deadline = timeout_ms ? timer_ticks() + timeout_ms : 0;
  for (;;)
  {
    uint64_t flags = spin_lock_irqsave(&udpe_lock);
    struct udpe *e = get(id);
    if (!e)
    {
      spin_unlock_irqrestore(&udpe_lock, flags);
      return -KERR_BADF;
    }
    if (e->tail != e->head)
    {
      struct udpe_slot *slot = &e->ring[e->tail];
      uint32_t copy = slot->length < length ? slot->length : length;
      memcpy(scratch, slot->data, copy);
      uint32_t source = slot->source;
      uint16_t source_port = slot->port;
      if (!peek)
        e->tail = (e->tail + 1) % UDPE_SLOTS;
      spin_unlock_irqrestore(&udpe_lock, flags);
      memcpy(buffer, scratch, copy);
      if (address)
        *address = source;
      if (port)
        *port = source_port;
      return (int32_t)copy;
    }
    spin_unlock_irqrestore(&udpe_lock, flags);
    if (nonblock)
      return -KERR_AGAIN;
    if (deadline && timer_ticks() >= deadline)
      return -KERR_AGAIN;
    if (signal_pending_current())
      return -KERR_INTR;
    scheduler_sleep(1);
  }
}

int32_t udpe_poll(int32_t id)
{
  uint64_t flags = spin_lock_irqsave(&udpe_lock);
  struct udpe *e = get(id);
  int32_t out = e ? KPOLLOUT : KPOLLNVAL;
  if (e && e->head != e->tail)
    out |= KPOLLIN;
  spin_unlock_irqrestore(&udpe_lock, flags);
  return out;
}
