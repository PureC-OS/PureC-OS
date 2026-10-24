#include "kernel/ipc/ipc.c"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t mock_ticks = 1000;
uint64_t timer_ticks(void) { return mock_ticks; }
void scheduler_sleep(uint32_t ms) { mock_ticks += ms ? ms : 1; }
void smp_reschedule_all(void) {}

void *oom_slab_alloc(uint64_t size) {
  void *p = malloc((size_t)size);
  if (p) memset(p, 0, (size_t)size);
  return p;
}
void oom_slab_free(void *ptr, uint64_t size) {
  (void)size;
  free(ptr);
}

static struct process proc_a;
static struct process proc_b;
static struct process *mock_current;
static bool mock_cap = true;

struct process *process_current(void) { return mock_current; }
bool process_has_capability(uint32_t cap) {
  (void)cap;
  return mock_cap;
}
struct process *process_lookup_by_pid(uint32_t pid) {
  if (proc_a.pid == pid && proc_a.state != PROCESS_FREE) return &proc_a;
  if (proc_b.pid == pid && proc_b.state != PROCESS_FREE) return &proc_b;
  return NULL;
}
void process_table_lock(void) {}
void process_table_unlock(void) {}

static void reset(void) {
  memset(&proc_a, 0, sizeof(proc_a));
  memset(&proc_b, 0, sizeof(proc_b));
  proc_a.pid = 10;
  proc_a.state = PROCESS_READY;
  proc_b.pid = 20;
  proc_b.state = PROCESS_READY;
  mock_current = &proc_a;
  mock_cap = true;
  mock_ticks = 1000;
}

static void test_roundtrip(void) {
  reset();
  assert(ipc_send(20, "hello", 6) == 0);
  mock_current = &proc_b;
  char buf[64];
  uint32_t from = 0;
  assert(ipc_recv_from(&from, buf, sizeof(buf), 0) == 6);
  assert(from == 10);
  assert(memcmp(buf, "hello", 6) == 0);
  ipc_cleanup(&proc_a);
  ipc_cleanup(&proc_b);
}

static void test_fifo(void) {
  reset();
  for (uint32_t i = 0; i < 5; i++) {
    char msg[8];
    msg[0] = (char)('a' + (int)i);
    assert(ipc_send(20, msg, 8) == 0);
  }
  mock_current = &proc_b;
  for (uint32_t i = 0; i < 5; i++) {
    char buf[8];
    assert(ipc_recv(buf, sizeof(buf), 0) == 8);
    assert(buf[0] == (char)('a' + (int)i));
  }
  ipc_cleanup(&proc_a);
  ipc_cleanup(&proc_b);
}

static void test_empty_and_full(void) {
  reset();
  mock_current = &proc_b;
  char buf[64];
  assert(ipc_recv(buf, sizeof(buf), 0) == IPC_ERR_EMPTY);
  mock_current = &proc_a;
  for (uint32_t i = 0; i < IPC_MAX_DEPTH; i++)
    assert(ipc_send(20, "x", 2) == 0);
  assert(ipc_send(20, "x", 2) == IPC_ERR_FULL);
  mock_current = &proc_b;
  assert(ipc_recv(buf, sizeof(buf), 0) == 2);
  mock_current = &proc_a;
  assert(ipc_send(20, "x", 2) == 0);
  ipc_cleanup(&proc_a);
  ipc_cleanup(&proc_b);
}

static void test_limits(void) {
  reset();
  char big[IPC_MAX_PAYLOAD + 16];
  assert(ipc_send(20, big, sizeof(big)) == IPC_ERR_TOO_BIG);
  assert(ipc_send(0, "x", 2) == IPC_ERR_INVALID);
  assert(ipc_send(20, NULL, 2) == IPC_ERR_INVALID);
  assert(ipc_send(99, "x", 2) == IPC_ERR_NO_PROCESS);
  proc_b.state = PROCESS_EXITED;
  assert(ipc_send(20, "x", 2) == IPC_ERR_NO_PROCESS);
  proc_b.state = PROCESS_READY;
  assert(ipc_send(20, "hello", 6) == 0);
  mock_current = &proc_b;
  char tiny[2];
  assert(ipc_recv(tiny, sizeof(tiny), 0) == IPC_ERR_TOO_SMALL);
  char buf[64];
  assert(ipc_recv(buf, sizeof(buf), 0) == 6);
  mock_cap = false;
  assert(ipc_send(20, "x", 2) == IPC_ERR_INVALID);
  assert(ipc_recv(buf, sizeof(buf), 0) == IPC_ERR_INVALID);
  mock_cap = true;
  ipc_cleanup(&proc_a);
  ipc_cleanup(&proc_b);
}

static void test_cleanup_drains(void) {
  reset();
  for (int i = 0; i < 10; i++) assert(ipc_send(20, "data", 5) == 0);
  ipc_cleanup(&proc_b);
  assert(proc_b.ipc_mailbox == NULL);
  mock_current = &proc_b;
  char buf[64];
  assert(ipc_recv(buf, sizeof(buf), 0) == IPC_ERR_EMPTY);
  ipc_cleanup(&proc_a);
  ipc_cleanup(&proc_b);
}

static void test_frame_size(void) {
  reset();
  char frame[1514];
  for (int i = 0; i < (int)sizeof(frame); i++) frame[i] = (char)(i & 0xFF);
  assert(ipc_send(20, frame, sizeof(frame)) == 0);
  mock_current = &proc_b;
  char buf[2048];
  assert(ipc_recv(buf, sizeof(buf), 0) == (int32_t)sizeof(frame));
  assert(memcmp(buf, frame, sizeof(frame)) == 0);
  ipc_cleanup(&proc_a);
  ipc_cleanup(&proc_b);
}

int main(void) {
  test_roundtrip();
  test_fifo();
  test_empty_and_full();
  test_limits();
  test_cleanup_drains();
  test_frame_size();
  puts("IPC mailbox tests passed");
  return 0;
}
