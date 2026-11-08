#include "unix.h"
#include "../../fs/vfs.h"
#include "../../lib/string.h"
#include "../../mm/pmm.h"
#include "../process/ofd.h"
#include "../process/process.h"
#include "../process/scheduler.h"
#include "../process/signal.h"
#include "../sync/mutex.h"
#include "../syscall/syscall.h"

#define UNIX_MAX 128
#define UNIX_PAGES 4
#define UNIX_CAPACITY (UNIX_PAGES * 4096U)
#define UNIX_WAITERS 8
#define UNIX_BACKLOG 16
#define UNIX_FDQ 8

struct fd_entry
{
  uint64_t position;
  uint32_t count;
  int32_t ofds[UNIX_FDS_PER_MESSAGE];
};

struct usock
{
  bool used;
  bool listening;
  bool bound;
  bool abstract_name;
  bool inherited_name;
  bool connected;
  bool peer_gone;
  bool eof_in;
  bool shut_rd;
  bool shut_wr;
  uint8_t type;
  int32_t peer;
  uint8_t *buffer;
  uint64_t phys;
  uint32_t head;
  uint32_t count;
  uint64_t in_total;
  uint64_t out_total;
  uint32_t name_length;
  uint8_t name[UNIX_PATH_MAX + 1];
  uint32_t backlog;
  uint32_t pending_count;
  int32_t pending[UNIX_BACKLOG];
  uint32_t fdq_count;
  struct fd_entry fdq[UNIX_FDQ];
  struct unix_cred creator;
  int32_t read_waiters[UNIX_WAITERS];
  int32_t write_waiters[UNIX_WAITERS];
};

static mutex_t unix_mutex;
static struct usock socks[UNIX_MAX];
static uint32_t autobind_counter;

static void waiters_clear(int32_t *list, int32_t tid)
{
  for (uint32_t i = 0; i < UNIX_WAITERS; i++)
    if (list[i] == tid)
      list[i] = 0;
}

static bool waiters_add(int32_t *list, int32_t tid)
{
  for (uint32_t i = 0; i < UNIX_WAITERS; i++)
    if (list[i] == tid)
      return true;
  for (uint32_t i = 0; i < UNIX_WAITERS; i++)
  {
    if (!list[i])
    {
      list[i] = tid;
      return true;
    }
  }
  return false;
}

static uint32_t waiters_take(int32_t *list, int32_t *out)
{
  uint32_t n = 0;
  for (uint32_t i = 0; i < UNIX_WAITERS; i++)
  {
    if (list[i])
    {
      out[n++] = list[i];
      list[i] = 0;
    }
  }
  return n;
}

static void wake_all(const int32_t *tids, uint32_t count)
{
  for (uint32_t i = 0; i < count; i++)
    scheduler_unblock(tids[i]);
}

static struct usock *sock_get(int32_t id)
{
  if (id < 0 || id >= UNIX_MAX || !socks[id].used)
    return 0;
  return &socks[id];
}

static void creator_fill(struct unix_cred *cred)
{
  cred->pid = process_current_pid();
  cred->uid = process_current_uid();
  cred->gid = process_current_gid();
}

static int32_t allocate_locked(int32_t type)
{
  if (type != UNIX_TYPE_STREAM)
    return -KERR_PROTOTYPE;
  for (int32_t i = 0; i < UNIX_MAX; i++)
  {
    if (socks[i].used)
      continue;
    uint64_t phys = pmm_allocate_contiguous(UNIX_PAGES);
    if (!phys)
      return -KERR_NOMEM;
    memset(&socks[i], 0, sizeof(socks[i]));
    socks[i].used = true;
    socks[i].type = (uint8_t)type;
    socks[i].peer = -1;
    socks[i].phys = phys;
    socks[i].buffer = (uint8_t *)pmm_physical_to_virtual(phys);
    creator_fill(&socks[i].creator);
    return i;
  }
  return -KERR_NFILE;
}

int32_t unix_create(int32_t type)
{
  mutex_lock(&unix_mutex);
  int32_t id = allocate_locked(type);
  mutex_unlock(&unix_mutex);
  return id;
}

int32_t unix_pair(int32_t type, int32_t ids[2])
{
  mutex_lock(&unix_mutex);
  int32_t first = allocate_locked(type);
  if (first < 0)
  {
    mutex_unlock(&unix_mutex);
    return first;
  }
  int32_t second = allocate_locked(type);
  if (second < 0)
  {
    pmm_free_contiguous(socks[first].phys, UNIX_PAGES);
    memset(&socks[first], 0, sizeof(socks[first]));
    mutex_unlock(&unix_mutex);
    return second;
  }
  socks[first].peer = second;
  socks[first].connected = true;
  socks[second].peer = first;
  socks[second].connected = true;
  mutex_unlock(&unix_mutex);
  ids[0] = first;
  ids[1] = second;
  return 0;
}

void unix_release(int32_t id)
{
  int32_t wake[UNIX_WAITERS * 4];
  uint32_t wake_count = 0;
  int32_t dropped[UNIX_FDQ * UNIX_FDS_PER_MESSAGE];
  uint32_t dropped_count = 0;
  int32_t orphans[UNIX_BACKLOG];
  uint32_t orphan_count = 0;
  mutex_lock(&unix_mutex);
  struct usock *s = sock_get(id);
  if (!s)
  {
    mutex_unlock(&unix_mutex);
    return;
  }
  if (s->peer >= 0)
  {
    struct usock *p = sock_get(s->peer);
    if (p)
    {
      p->peer = -1;
      p->peer_gone = true;
      wake_count += waiters_take(p->read_waiters, wake + wake_count);
      wake_count += waiters_take(p->write_waiters, wake + wake_count);
    }
  }
  for (uint32_t i = 0; i < s->pending_count; i++)
    orphans[orphan_count++] = s->pending[i];
  for (uint32_t e = 0; e < s->fdq_count; e++)
    for (uint32_t i = 0; i < s->fdq[e].count; i++)
      dropped[dropped_count++] = s->fdq[e].ofds[i];
  pmm_free_contiguous(s->phys, UNIX_PAGES);
  memset(s, 0, sizeof(*s));
  mutex_unlock(&unix_mutex);
  wake_all(wake, wake_count);
  for (uint32_t i = 0; i < orphan_count; i++)
    unix_release(orphans[i]);
  for (uint32_t i = 0; i < dropped_count; i++)
    ofd_release(dropped[i]);
}

static bool name_matches(const struct usock *s, bool abstract_name,
                         const uint8_t *name, uint32_t length)
{
  return s->bound && s->abstract_name == abstract_name &&
         s->name_length == length && memcmp(s->name, name, length) == 0;
}

static int32_t normalize_name(const uint8_t *name, uint32_t length,
                              bool *abstract_name, uint8_t *out,
                              uint32_t *out_length)
{
  if (length > UNIX_PATH_MAX)
    return -KERR_INVAL;
  if (length && name[0] == 0)
  {
    *abstract_name = true;
    memcpy(out, name, length);
    *out_length = length;
    return 0;
  }
  uint32_t n = 0;
  while (n < length && name[n])
    n++;
  if (!n)
    return -KERR_INVAL;
  *abstract_name = false;
  memcpy(out, name, n);
  out[n] = 0;
  *out_length = n;
  return 0;
}

int32_t unix_bind(int32_t id, const uint8_t *name, uint32_t length)
{
  uint8_t key[UNIX_PATH_MAX + 1];
  uint32_t key_length = 0;
  bool abstract_name = false;
  if (length == 0)
  {
    abstract_name = true;
    key[0] = 0;
    key_length = 6;
  }
  else
  {
    int32_t status = normalize_name(name, length, &abstract_name, key, &key_length);
    if (status < 0)
      return status;
  }
  mutex_lock(&unix_mutex);
  struct usock *s = sock_get(id);
  if (!s)
  {
    mutex_unlock(&unix_mutex);
    return -KERR_BADF;
  }
  if (s->bound)
  {
    mutex_unlock(&unix_mutex);
    return -KERR_INVAL;
  }
  if (length == 0)
  {
    for (;;)
    {
      uint32_t value = ++autobind_counter & 0xFFFFFU;
      for (uint32_t i = 0; i < 5; i++)
        key[1 + i] = "0123456789abcdef"[(value >> ((4 - i) * 4)) & 15];
      bool clash = false;
      for (int32_t i = 0; i < UNIX_MAX; i++)
        if (socks[i].used && name_matches(&socks[i], true, key, key_length))
          clash = true;
      if (!clash)
        break;
    }
  }
  else
  {
    for (int32_t i = 0; i < UNIX_MAX; i++)
    {
      if (socks[i].used && name_matches(&socks[i], abstract_name, key, key_length))
      {
        mutex_unlock(&unix_mutex);
        return -KERR_ADDRINUSE;
      }
    }
  }
  s->bound = true;
  s->abstract_name = abstract_name;
  s->name_length = key_length;
  memcpy(s->name, key, key_length);
  s->name[key_length] = 0;
  mutex_unlock(&unix_mutex);
  if (abstract_name)
    return 0;
  if (!filesystem_syscall_acquire())
    goto fail_again;
  struct file_stat_info info;
  if (vfs_stat((const char *)key, &info) == 0)
  {
    filesystem_syscall_unlock();
    goto fail_in_use;
  }
  int32_t created = vfs_create_file((const char *)key);
  filesystem_syscall_unlock();
  if (created < 0)
  {
    mutex_lock(&unix_mutex);
    s = sock_get(id);
    if (s)
      s->bound = false;
    mutex_unlock(&unix_mutex);
    return kerr_from_vfs(created);
  }
  return 0;
fail_again:
  mutex_lock(&unix_mutex);
  s = sock_get(id);
  if (s)
    s->bound = false;
  mutex_unlock(&unix_mutex);
  return -KERR_AGAIN;
fail_in_use:
  mutex_lock(&unix_mutex);
  s = sock_get(id);
  if (s)
    s->bound = false;
  mutex_unlock(&unix_mutex);
  return -KERR_ADDRINUSE;
}

int32_t unix_listen(int32_t id, int32_t backlog)
{
  mutex_lock(&unix_mutex);
  struct usock *s = sock_get(id);
  int32_t result = 0;
  if (!s)
    result = -KERR_BADF;
  else if (!s->bound || s->connected)
    result = -KERR_INVAL;
  else
  {
    s->listening = true;
    s->backlog = backlog <= 0 ? 1U : (backlog > UNIX_BACKLOG ? UNIX_BACKLOG : (uint32_t)backlog);
  }
  mutex_unlock(&unix_mutex);
  return result;
}

int32_t unix_connect(int32_t id, const uint8_t *name, uint32_t length,
                     bool nonblock)
{
  uint8_t key[UNIX_PATH_MAX + 1];
  uint32_t key_length = 0;
  bool abstract_name = false;
  int32_t status = normalize_name(name, length, &abstract_name, key, &key_length);
  if (status < 0)
    return status;
  bool file_present = true;
  if (!abstract_name)
  {
    if (!filesystem_syscall_acquire())
      return -KERR_AGAIN;
    struct file_stat_info info;
    file_present = vfs_stat((const char *)key, &info) == 0;
    filesystem_syscall_unlock();
  }
  int32_t self_tid = scheduler_current_tid();
  for (;;)
  {
    int32_t wake[UNIX_WAITERS];
    uint32_t wake_count = 0;
    mutex_lock(&unix_mutex);
    struct usock *client = sock_get(id);
    if (!client)
    {
      mutex_unlock(&unix_mutex);
      return -KERR_BADF;
    }
    if (client->connected || client->listening)
    {
      mutex_unlock(&unix_mutex);
      return -KERR_ISCONN;
    }
    struct usock *listener = 0;
    for (int32_t i = 0; i < UNIX_MAX; i++)
    {
      if (socks[i].used && socks[i].listening &&
          name_matches(&socks[i], abstract_name, key, key_length))
      {
        listener = &socks[i];
        break;
      }
    }
    if (!listener || !file_present)
    {
      mutex_unlock(&unix_mutex);
      return file_present ? -KERR_CONNREFUSED : -KERR_NOENT;
    }
    if (listener->pending_count >= listener->backlog)
    {
      if (nonblock)
      {
        mutex_unlock(&unix_mutex);
        return -KERR_AGAIN;
      }
      if (signal_pending_current())
      {
        mutex_unlock(&unix_mutex);
        return -KERR_INTR;
      }
      bool registered = waiters_add(listener->write_waiters, self_tid);
      mutex_unlock(&unix_mutex);
      if (registered)
        scheduler_block();
      else
        scheduler_sleep(1);
      continue;
    }
    int32_t server_id = allocate_locked(UNIX_TYPE_STREAM);
    if (server_id < 0)
    {
      mutex_unlock(&unix_mutex);
      return server_id;
    }
    struct usock *server = &socks[server_id];
    struct unix_cred accepted_creator = listener->creator;
    server->creator = accepted_creator;
    server->connected = true;
    server->peer = id;
    server->inherited_name = true;
    server->abstract_name = listener->abstract_name;
    server->name_length = listener->name_length;
    memcpy(server->name, listener->name, sizeof(server->name));
    client->connected = true;
    client->peer = server_id;
    listener->pending[listener->pending_count++] = server_id;
    wake_count = waiters_take(listener->read_waiters, wake);
    mutex_unlock(&unix_mutex);
    wake_all(wake, wake_count);
    return 0;
  }
}

int32_t unix_accept(int32_t id, bool nonblock, int32_t *new_id)
{
  int32_t self_tid = scheduler_current_tid();
  for (;;)
  {
    int32_t wake[UNIX_WAITERS];
    uint32_t wake_count = 0;
    mutex_lock(&unix_mutex);
    struct usock *s = sock_get(id);
    if (!s)
    {
      mutex_unlock(&unix_mutex);
      return -KERR_BADF;
    }
    if (!s->listening)
    {
      mutex_unlock(&unix_mutex);
      return -KERR_INVAL;
    }
    waiters_clear(s->read_waiters, self_tid);
    if (s->pending_count)
    {
      *new_id = s->pending[0];
      for (uint32_t i = 1; i < s->pending_count; i++)
        s->pending[i - 1] = s->pending[i];
      s->pending_count--;
      wake_count = waiters_take(s->write_waiters, wake);
      mutex_unlock(&unix_mutex);
      wake_all(wake, wake_count);
      return 0;
    }
    if (nonblock)
    {
      mutex_unlock(&unix_mutex);
      return -KERR_AGAIN;
    }
    if (signal_pending_current())
    {
      mutex_unlock(&unix_mutex);
      return -KERR_INTR;
    }
    bool registered = waiters_add(s->read_waiters, self_tid);
    mutex_unlock(&unix_mutex);
    if (registered)
      scheduler_block();
    else
      scheduler_sleep(1);
  }
}

int32_t unix_name(int32_t id, bool peer, uint8_t *out, uint32_t capacity)
{
  mutex_lock(&unix_mutex);
  struct usock *s = sock_get(id);
  if (!s)
  {
    mutex_unlock(&unix_mutex);
    return -KERR_BADF;
  }
  if (peer)
  {
    if (!s->connected || s->peer < 0)
    {
      mutex_unlock(&unix_mutex);
      return -KERR_NOTCONN;
    }
    s = sock_get(s->peer);
    if (!s)
    {
      mutex_unlock(&unix_mutex);
      return -KERR_NOTCONN;
    }
  }
  uint32_t length = 0;
  if (s->bound || s->inherited_name)
  {
    length = s->name_length + (s->abstract_name ? 0U : 1U);
    uint32_t copy = length < capacity ? length : capacity;
    memcpy(out, s->name, copy);
  }
  mutex_unlock(&unix_mutex);
  return (int32_t)length;
}

int32_t unix_shutdown(int32_t id, int32_t how)
{
  int32_t wake[UNIX_WAITERS * 4];
  uint32_t wake_count = 0;
  if (how < 0 || how > 2)
    return -KERR_INVAL;
  mutex_lock(&unix_mutex);
  struct usock *s = sock_get(id);
  if (!s)
  {
    mutex_unlock(&unix_mutex);
    return -KERR_BADF;
  }
  if (!s->connected)
  {
    mutex_unlock(&unix_mutex);
    return -KERR_NOTCONN;
  }
  struct usock *p = s->peer >= 0 ? sock_get(s->peer) : 0;
  if (how == 0 || how == 2)
  {
    s->shut_rd = true;
    wake_count += waiters_take(s->read_waiters, wake + wake_count);
    if (p)
      wake_count += waiters_take(p->write_waiters, wake + wake_count);
  }
  if (how == 1 || how == 2)
  {
    s->shut_wr = true;
    if (p)
    {
      p->eof_in = true;
      wake_count += waiters_take(p->read_waiters, wake + wake_count);
    }
  }
  mutex_unlock(&unix_mutex);
  wake_all(wake, wake_count);
  return 0;
}

int32_t unix_peer_cred(int32_t id, struct unix_cred *out)
{
  mutex_lock(&unix_mutex);
  struct usock *s = sock_get(id);
  int32_t result = 0;
  if (!s)
    result = -KERR_BADF;
  else if (s->peer >= 0 && sock_get(s->peer))
    *out = socks[s->peer].creator;
  else
    *out = s->creator;
  mutex_unlock(&unix_mutex);
  return result;
}

int32_t unix_send(int32_t id, const struct unix_iov *iov, uint32_t count,
                  const int32_t *ofds, uint32_t ofd_count, bool nonblock,
                  uint32_t flags)
{
  uint64_t total = 0;
  for (uint32_t i = 0; i < count; i++)
    total += iov[i].length;
  if (ofd_count > UNIX_FDS_PER_MESSAGE || (ofd_count && !total))
    return -KERR_INVAL;
  int32_t self_tid = scheduler_current_tid();
  bool attach = ofd_count != 0;
  uint64_t written = 0;
  uint32_t segment = 0;
  uint64_t offset = 0;
  while (written < total)
  {
    while (segment < count && offset >= iov[segment].length)
    {
      segment++;
      offset = 0;
    }
    int32_t wake[UNIX_WAITERS];
    uint32_t wake_count = 0;
    mutex_lock(&unix_mutex);
    struct usock *s = sock_get(id);
    if (!s)
    {
      mutex_unlock(&unix_mutex);
      return written ? (int32_t)written : -KERR_BADF;
    }
    if (!s->connected)
    {
      mutex_unlock(&unix_mutex);
      return written ? (int32_t)written : -KERR_NOTCONN;
    }
    struct usock *p = s->peer >= 0 ? sock_get(s->peer) : 0;
    if (s->shut_wr || !p || p->shut_rd)
    {
      mutex_unlock(&unix_mutex);
      if (!(flags & UNIX_MSG_NOSIGNAL))
        signal_raise_current(SIGPIPE);
      return written ? (int32_t)written : -KERR_PIPE;
    }
    waiters_clear(p->write_waiters, self_tid);
    uint32_t space = UNIX_CAPACITY - p->count;
    bool slot_ready = !attach || p->fdq_count < UNIX_FDQ;
    if (space && slot_ready)
    {
      uint64_t amount = total - written;
      if (amount > space)
        amount = space;
      uint64_t room = iov[segment].length - offset;
      if (amount > room)
        amount = room;
      if (attach)
      {
        struct fd_entry *entry = &p->fdq[p->fdq_count++];
        entry->position = p->in_total;
        entry->count = ofd_count;
        for (uint32_t i = 0; i < ofd_count; i++)
          entry->ofds[i] = ofds[i];
        attach = false;
      }
      const uint8_t *source = (const uint8_t *)(uintptr_t)(iov[segment].base + offset);
      uint32_t tail = (p->head + p->count) % UNIX_CAPACITY;
      for (uint64_t i = 0; i < amount; i++)
        p->buffer[(tail + i) % UNIX_CAPACITY] = source[i];
      p->count += (uint32_t)amount;
      p->in_total += amount;
      written += amount;
      offset += amount;
      wake_count = waiters_take(p->read_waiters, wake);
      mutex_unlock(&unix_mutex);
      wake_all(wake, wake_count);
      continue;
    }
    if (nonblock || (flags & UNIX_MSG_DONTWAIT))
    {
      mutex_unlock(&unix_mutex);
      return written ? (int32_t)written : -KERR_AGAIN;
    }
    if (signal_pending_current())
    {
      mutex_unlock(&unix_mutex);
      return written ? (int32_t)written : -KERR_INTR;
    }
    bool registered = waiters_add(p->write_waiters, self_tid);
    mutex_unlock(&unix_mutex);
    if (registered)
      scheduler_block();
    else
      scheduler_sleep(1);
  }
  return (int32_t)written;
}

int32_t unix_recv(int32_t id, const struct unix_iov *iov, uint32_t count,
                  int32_t *ofds, uint32_t *ofd_count, bool *truncated,
                  uint32_t ofd_capacity, bool nonblock, uint32_t flags)
{
  uint64_t capacity = 0;
  for (uint32_t i = 0; i < count; i++)
    capacity += iov[i].length;
  if (ofd_count)
    *ofd_count = 0;
  if (truncated)
    *truncated = false;
  if (!capacity)
    return 0;
  int32_t self_tid = scheduler_current_tid();
  for (;;)
  {
    int32_t wake[UNIX_WAITERS];
    uint32_t wake_count = 0;
    mutex_lock(&unix_mutex);
    struct usock *s = sock_get(id);
    if (!s)
    {
      mutex_unlock(&unix_mutex);
      return -KERR_BADF;
    }
    if (s->listening || !s->connected)
    {
      mutex_unlock(&unix_mutex);
      return -KERR_NOTCONN;
    }
    waiters_clear(s->read_waiters, self_tid);
    if (s->shut_rd)
    {
      mutex_unlock(&unix_mutex);
      return 0;
    }
    if (s->count)
    {
      uint64_t limit = capacity < s->count ? capacity : s->count;
      struct fd_entry *entry = 0;
      if (s->fdq_count)
      {
        if (s->fdq[0].position == s->out_total)
        {
          entry = &s->fdq[0];
          if (s->fdq_count > 1 && s->fdq[1].position - s->out_total < limit)
            limit = s->fdq[1].position - s->out_total;
        }
        else if (s->fdq[0].position - s->out_total < limit)
          limit = s->fdq[0].position - s->out_total;
      }
      bool peek = (flags & UNIX_MSG_PEEK) != 0;
      uint64_t copied = 0;
      uint32_t cursor = s->head;
      for (uint32_t i = 0; i < count && copied < limit; i++)
      {
        uint64_t take = iov[i].length;
        if (take > limit - copied)
          take = limit - copied;
        uint8_t *target = (uint8_t *)(uintptr_t)iov[i].base;
        for (uint64_t j = 0; j < take; j++)
          target[j] = s->buffer[(cursor + j) % UNIX_CAPACITY];
        cursor = (cursor + (uint32_t)take) % UNIX_CAPACITY;
        copied += take;
      }
      if (!peek)
      {
        s->head = (s->head + (uint32_t)copied) % UNIX_CAPACITY;
        s->count -= (uint32_t)copied;
        s->out_total += copied;
        if (entry && ofd_count)
        {
          uint32_t deliver = entry->count < ofd_capacity ? entry->count : ofd_capacity;
          for (uint32_t i = 0; i < deliver; i++)
            ofds[i] = entry->ofds[i];
          *ofd_count = deliver;
          for (uint32_t i = deliver; i < entry->count; i++)
            ofd_release(entry->ofds[i]);
          if (truncated && deliver < entry->count)
            *truncated = true;
        }
        else if (entry)
        {
          for (uint32_t i = 0; i < entry->count; i++)
            ofd_release(entry->ofds[i]);
          if (truncated)
            *truncated = true;
        }
        if (entry)
        {
          for (uint32_t i = 1; i < s->fdq_count; i++)
            s->fdq[i - 1] = s->fdq[i];
          s->fdq_count--;
        }
        wake_count = waiters_take(s->write_waiters, wake);
      }
      mutex_unlock(&unix_mutex);
      wake_all(wake, wake_count);
      return (int32_t)copied;
    }
    if (s->peer_gone || s->eof_in)
    {
      mutex_unlock(&unix_mutex);
      return 0;
    }
    if (nonblock || (flags & UNIX_MSG_DONTWAIT))
    {
      mutex_unlock(&unix_mutex);
      return -KERR_AGAIN;
    }
    if (signal_pending_current())
    {
      mutex_unlock(&unix_mutex);
      return -KERR_INTR;
    }
    bool registered = waiters_add(s->read_waiters, self_tid);
    mutex_unlock(&unix_mutex);
    if (registered)
      scheduler_block();
    else
      scheduler_sleep(1);
  }
}

int32_t unix_poll(int32_t id)
{
  int32_t revents = 0;
  mutex_lock(&unix_mutex);
  struct usock *s = sock_get(id);
  if (!s)
  {
    mutex_unlock(&unix_mutex);
    return KPOLLNVAL;
  }
  if (s->listening)
  {
    if (s->pending_count)
      revents |= KPOLLIN;
  }
  else
  {
    if (s->count || s->peer_gone || s->eof_in || s->shut_rd)
      revents |= KPOLLIN;
    struct usock *p = s->peer >= 0 ? sock_get(s->peer) : 0;
    if (s->connected && p && !s->shut_wr && p->count < UNIX_CAPACITY)
      revents |= KPOLLOUT;
    if (s->peer_gone || (s->shut_rd && s->shut_wr))
      revents |= KPOLLHUP;
  }
  mutex_unlock(&unix_mutex);
  return revents;
}
