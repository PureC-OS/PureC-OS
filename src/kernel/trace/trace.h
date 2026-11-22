#pragma once
#include <stdbool.h>
#include <stdint.h>

#define TRACE_FOLLOW 0x1u
#define TRACE_STOP_MODE 0x2u

#define TRACE_EV_ENTER 1u
#define TRACE_EV_EXIT 2u
#define TRACE_EV_PROC_EXIT 3u
#define TRACE_EV_STOPPED 4u

#define TRACE_REC_STR1 0x1u
#define TRACE_REC_STR2 0x2u

#define TRACE_OP_CONTINUE 1u
#define TRACE_OP_SET_STOP_MASK 2u
#define TRACE_OP_DETACH 3u
#define TRACE_OP_STATUS 4u

#define TRACE_STR_MAX 96
#define TRACE_MASK_WORDS 8

struct trace_record {
  uint32_t type;
  uint32_t flags;
  uint32_t pid;
  uint32_t tid;
  uint32_t nr;
  uint32_t ppid;
  uint64_t args[6];
  int64_t ret;
  uint64_t time_ns;
  uint64_t duration_ns;
  char comm[24];
  char str[TRACE_STR_MAX];
};

struct trace_status {
  uint32_t root_pid;
  uint32_t stopped_pid;
  uint32_t stopped_tid;
  uint32_t stopped_nr;
  uint64_t dropped;
  uint64_t pending;
};

struct process;
struct syscall_regs;

int32_t trace_attach(uint32_t pid, uint32_t flags);
int32_t trace_read(int32_t id, struct trace_record *out, uint32_t max, uint32_t timeout_ms);
int32_t trace_control(int32_t id, uint32_t op, uint64_t arg);
void trace_fork(struct process *parent, struct process *child);
void trace_process_exit(struct process *process, int32_t status, int32_t signal);
uint64_t trace_syscall_enter(struct process *p, uint64_t nr, const uint64_t args[6]);
void trace_syscall_exit(struct process *p, uint64_t nr, const uint64_t args[6], int64_t ret, uint64_t start);
