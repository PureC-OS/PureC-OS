#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#define SCHEDULER_STACK_PAGES 4
#define SCHEDULER_STACK_SIZE (SCHEDULER_STACK_PAGES*4096u)
#define SCHEDULER_TIME_SLICE_MS 5

struct process;

enum thread_state {
    THREAD_FREE = 0,
    THREAD_READY = 1,
    THREAD_RUNNING = 2,
    THREAD_BLOCKED = 3,
    THREAD_TERMINATED = 4
};

struct thread {
    uint64_t rsp;
    void (*entry)(void *arg);
    void *arg;
    enum thread_state state;
    uint8_t priority;
    int16_t affinity;
    uint32_t id;
    char name[32];
    uint32_t ticks_remaining;
    uint64_t runtime_ticks;
    uint64_t wake_tick;
    uint64_t address_space;
    struct process *process;
    bool user_mode;
    bool idle;
    bool kernel_only;
    bool wake_pending;
    int16_t running_cpu;
    uint32_t cpu_mask;
    uint64_t migrations;
    uint8_t stack[SCHEDULER_STACK_SIZE] __attribute__((aligned(16)));
    uint8_t fpu_state[512] __attribute__((aligned(16)));
};

void scheduler_init(void);
int scheduler_create_thread(void (*entry)(void *arg), void *arg, const char *name, uint8_t priority, int16_t affinity);
int scheduler_create_user_thread(void (*entry)(void *arg), void *arg,
                                 const char *name, uint8_t priority,
                                 int16_t affinity, uint64_t address_space,
                                 struct process *process);
void scheduler_yield(void);
bool scheduler_preempt_disable(void);
void scheduler_preempt_enable(void);
void scheduler_sleep(uint32_t milliseconds);
void scheduler_block(void);
void scheduler_unblock(int tid);
void scheduler_exit(void);
void scheduler_free_thread(struct thread *t);
void scheduler_start(void);
struct thread *scheduler_current_thread(void);
int scheduler_current_tid(void);
uint32_t scheduler_thread_count(void);
uint64_t scheduler_thread_runtime_ticks(int tid);
uint64_t scheduler_total_ticks(void);
uint64_t scheduler_idle_ticks(void);
bool scheduler_cpu_ticks(uint32_t id, uint64_t *total, uint64_t *idle);
void scheduler_set_affinity(int tid, int16_t core);
int scheduler_get_core_count(void);

void scheduler_on_timer_interrupt(void);
void scheduler_asm_switch(uint64_t *old_rsp, uint64_t *new_rsp);

void scheduler_init_cpu(void);
void scheduler_idle_loop(void) __attribute__((noreturn));
bool scheduler_is_running(void);
void scheduler_enter_kernel(void);
void scheduler_leave_kernel(void);
void scheduler_on_reschedule_interrupt(void);
bool scheduler_thread_stopped(int tid);
uint32_t scheduler_active_mask(void);
