#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <arch/arch.h>

#include "cpu.h"

struct thread;

struct percpu {
    struct percpu *self;
    uint32_t cpu_id;
    uint32_t reserved;
    struct thread *current;
    uint64_t kstack_top;
    uint64_t user_rsp;
} __attribute__((aligned(64)));

#define PERCPU_OFF_SELF 0
#define PERCPU_OFF_CPU_ID 8
#define PERCPU_OFF_CURRENT 16
#define PERCPU_OFF_KSTACK_TOP 24
#define PERCPU_OFF_USER_RSP 32

_Static_assert(offsetof(struct percpu, self) == PERCPU_OFF_SELF,
               "percpu self offset");
_Static_assert(offsetof(struct percpu, cpu_id) == PERCPU_OFF_CPU_ID,
               "percpu cpu_id offset");
_Static_assert(offsetof(struct percpu, current) == PERCPU_OFF_CURRENT,
               "percpu current offset");
_Static_assert(offsetof(struct percpu, kstack_top) == PERCPU_OFF_KSTACK_TOP,
               "percpu kstack_top offset");
_Static_assert(offsetof(struct percpu, user_rsp) == PERCPU_OFF_USER_RSP,
               "percpu user_rsp offset");

bool percpu_install(uint32_t cpu_id);
struct percpu *percpu_of(uint32_t cpu_id);
void percpu_set_current(uint32_t cpu_id, struct thread *thread);

static inline uint32_t percpu_cpu_id(void)
{
    return ARCH_PERCPU_LOAD32(PERCPU_OFF_CPU_ID);
}

static inline struct thread *percpu_current(void)
{
    return (struct thread *)(uintptr_t)ARCH_PERCPU_LOAD64(PERCPU_OFF_CURRENT);
}
