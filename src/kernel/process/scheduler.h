#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define SCHEDULER_MAX_THREADS 64
#define SCHEDULER_STACK_SIZE 16384
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
    uint32_t waited;
    uint64_t wake_tick;
    uint64_t address_space;
    struct process *process;
    bool user_mode;
    void *wait_channel;
    struct thread *wait_next;
    volatile bool wait_woken;
    int16_t last_cpu;
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
void scheduler_sleep(uint32_t milliseconds);
void scheduler_block(void);
void scheduler_unblock(int tid);
void scheduler_exit(void);
void scheduler_start(void);
void scheduler_enter(void);
bool scheduler_is_started(void);
struct thread *scheduler_current_thread(void);
int scheduler_current_tid(void);
uint32_t scheduler_thread_count(void);
uint64_t scheduler_thread_runtime_ticks(int tid);
uint64_t scheduler_total_ticks(void);
uint64_t scheduler_idle_ticks(void);
void scheduler_set_affinity(int tid, int16_t core);
int scheduler_get_core_count(void);
void scheduler_wait_queued(void);
void scheduler_make_ready(struct thread *thread);

void scheduler_on_timer_interrupt(void);
void scheduler_asm_switch(uint64_t *old_rsp, uint64_t *new_rsp);
