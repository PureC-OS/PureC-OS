#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cpu.h"

struct thread;

struct percpu {
    struct percpu *self;
    uint32_t cpu_id;
    uint32_t reserved;
    struct thread *current;
} __attribute__((aligned(64)));

#define PERCPU_OFF_SELF 0
#define PERCPU_OFF_CPU_ID 8
#define PERCPU_OFF_CURRENT 16

_Static_assert(offsetof(struct percpu, self) == PERCPU_OFF_SELF,
               "percpu self offset");
_Static_assert(offsetof(struct percpu, cpu_id) == PERCPU_OFF_CPU_ID,
               "percpu cpu_id offset");
_Static_assert(offsetof(struct percpu, current) == PERCPU_OFF_CURRENT,
               "percpu current offset");

bool percpu_install(uint32_t cpu_id);
struct percpu *percpu_of(uint32_t cpu_id);
void percpu_set_current(uint32_t cpu_id, struct thread *thread);

static inline uint32_t percpu_cpu_id(void)
{
    uint32_t id;
    __asm__ volatile("movl %%gs:%c1, %0"
                     : "=r"(id)
                     : "i"(PERCPU_OFF_CPU_ID));
    return id;
}

static inline struct thread *percpu_current(void)
{
    struct thread *thread;
    __asm__ volatile("movq %%gs:%c1, %0"
                     : "=r"(thread)
                     : "i"(PERCPU_OFF_CURRENT));
    return thread;
}
