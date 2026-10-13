#include "ipc.h"
#include "../../drivers/interrupts/timer.h"
#include "../../lib/string.h"
#include "../../mm/oom/oom_slab.h"
#include "../process/process.h"
#include "../process/scheduler.h"
#include "../smp/smp.h"
#include "../sync/spinlock.h"

static spinlock_t ipc_lock = SPINLOCK_INIT;

static struct ipc_mbox *mbox_get(struct process *process, bool create) {
  if (!process)
    return NULL;
  if (!process->ipc_mailbox && create) {
    struct ipc_mbox *box =
        (struct ipc_mbox *)oom_slab_alloc(sizeof(struct ipc_mbox));
    if (!box)
      return NULL;
    memset(box, 0, sizeof(struct ipc_mbox));
    process->ipc_mailbox = box;
  }
  return process->ipc_mailbox;
}

static int32_t recv_locked(struct process *self, uint32_t *from,
                           void *buffer, uint32_t capacity) {
  struct ipc_mbox *box = self->ipc_mailbox;
  if (!box || !box->head)
    return IPC_ERR_EMPTY;
  struct ipc_msg *msg = box->head;
  if (msg->size > capacity)
    return IPC_ERR_TOO_SMALL;
  memcpy(buffer, msg->data, msg->size);
  if (from)
    *from = msg->from;
  int32_t got = (int32_t)msg->size;
  box->head = msg->next;
  if (!box->head)
    box->tail = NULL;
  box->count--;
  oom_slab_free(msg, sizeof(struct ipc_msg) + msg->size);
  return got;
}

int32_t ipc_send(uint32_t dest_pid, const void *data, uint32_t size) {
  if (!dest_pid || !data || !size || size > IPC_MAX_PAYLOAD)
    return size > IPC_MAX_PAYLOAD ? IPC_ERR_TOO_BIG : IPC_ERR_INVALID;
  if (!process_has_capability(PROCESS_CAP_NET_ADMIN))
    return IPC_ERR_INVALID;
  struct process *self = process_current();
  process_table_lock();
  struct process *target = process_lookup_by_pid(dest_pid);
  if (!target || target->state == PROCESS_EXITED ||
      target->state == PROCESS_FREE) {
    process_table_unlock();
    return IPC_ERR_NO_PROCESS;
  }
  uint64_t flags = spin_lock_irqsave(&ipc_lock);
  struct ipc_mbox *box = mbox_get(target, true);
  if (!box) {
    spin_unlock_irqrestore(&ipc_lock, flags);
    process_table_unlock();
    return IPC_ERR_NO_PROCESS;
  }
  if (box->count >= IPC_MAX_DEPTH) {
    spin_unlock_irqrestore(&ipc_lock, flags);
    process_table_unlock();
    return IPC_ERR_FULL;
  }
  struct ipc_msg *msg =
      (struct ipc_msg *)oom_slab_alloc(sizeof(struct ipc_msg) + size);
  if (!msg) {
    spin_unlock_irqrestore(&ipc_lock, flags);
    process_table_unlock();
    return IPC_ERR_FULL;
  }
  msg->next = NULL;
  msg->from = self ? self->pid : 0;
  msg->size = size;
  memcpy(msg->data, data, size);
  if (box->tail)
    box->tail->next = msg;
  else
    box->head = msg;
  box->tail = msg;
  box->count++;
  spin_unlock_irqrestore(&ipc_lock, flags);
  process_table_unlock();
  smp_reschedule_all();
  return 0;
}

static int32_t ipc_recv_any(uint32_t *from, void *buffer, uint32_t capacity,
                            uint32_t wait_ms) {
  if (!buffer || !capacity)
    return IPC_ERR_INVALID;
  if (!process_has_capability(PROCESS_CAP_NET_ADMIN))
    return IPC_ERR_INVALID;
  struct process *self = process_current();
  if (!self)
    return IPC_ERR_INVALID;
  if (wait_ms > IPC_WAIT_MAX_MS)
    wait_ms = IPC_WAIT_MAX_MS;
  uint64_t deadline = timer_ticks() + wait_ms;
  for (;;) {
    uint64_t flags = spin_lock_irqsave(&ipc_lock);
    int32_t rc = recv_locked(self, from, buffer, capacity);
    spin_unlock_irqrestore(&ipc_lock, flags);
    if (rc != IPC_ERR_EMPTY)
      return rc;
    uint64_t now = timer_ticks();
    if (wait_ms == 0 || now >= deadline)
      return IPC_ERR_EMPTY;
    scheduler_sleep(1);
  }
}

int32_t ipc_recv(void *buffer, uint32_t capacity, uint32_t wait_ms) {
  return ipc_recv_any(NULL, buffer, capacity, wait_ms);
}

int32_t ipc_recv_from(uint32_t *from_pid, void *buffer, uint32_t capacity,
                      uint32_t wait_ms) {
  return ipc_recv_any(from_pid, buffer, capacity, wait_ms);
}

void ipc_cleanup(struct process *process) {
  if (!process || !process->ipc_mailbox)
    return;
  uint64_t flags = spin_lock_irqsave(&ipc_lock);
  struct ipc_mbox *box = process->ipc_mailbox;
  process->ipc_mailbox = NULL;
  struct ipc_msg *msg = box->head;
  box->head = NULL;
  box->tail = NULL;
  box->count = 0;
  spin_unlock_irqrestore(&ipc_lock, flags);
  while (msg) {
    struct ipc_msg *next = msg->next;
    oom_slab_free(msg, sizeof(struct ipc_msg) + msg->size);
    msg = next;
  }
  oom_slab_free(box, sizeof(struct ipc_mbox));
}
