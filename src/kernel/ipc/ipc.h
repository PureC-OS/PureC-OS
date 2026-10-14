#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct process;

#define IPC_MAX_PAYLOAD 2048
#define IPC_MAX_DEPTH 64
#define IPC_WAIT_MAX_MS 60000

#define IPC_ERR_INVALID -1
#define IPC_ERR_NO_PROCESS -2
#define IPC_ERR_EMPTY -2
#define IPC_ERR_FULL -3
#define IPC_ERR_TOO_BIG -4
#define IPC_ERR_TOO_SMALL -3

struct ipc_msg {
  struct ipc_msg *next;
  uint32_t from;
  uint32_t size;
  uint8_t data[];
};

struct ipc_mbox {
  struct ipc_msg *head;
  struct ipc_msg *tail;
  uint32_t count;
};

int32_t ipc_send(uint32_t dest_pid, const void *data, uint32_t size);
int32_t ipc_recv(void *buffer, uint32_t capacity, uint32_t wait_ms);
int32_t ipc_recv_from(uint32_t *from_pid, void *buffer, uint32_t capacity,
                      uint32_t wait_ms);
void ipc_cleanup(struct process *process);
