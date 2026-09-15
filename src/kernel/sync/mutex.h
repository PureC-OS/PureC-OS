#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "spinlock.h"

struct thread;

struct mutex {
    spinlock_t guard;
    volatile int32_t locked;
    volatile int32_t owner;
    struct thread *head;
    struct thread *tail;
};

void mutex_init(struct mutex *mutex);
void mutex_lock(struct mutex *mutex);
bool mutex_try_lock(struct mutex *mutex);
void mutex_unlock(struct mutex *mutex);
