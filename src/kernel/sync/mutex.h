#pragma once
#include "spinlock.h"

/* Thread-owned recursive mutex for process/VFS operations that can sleep.
   Not usable in interrupt context. Metadata is protected by an IRQ-safe lock. */
typedef struct {
    spinlock_t lock;
    uint64_t owner;
    unsigned depth;
} mutex_t;
void mutex_lock(mutex_t *mutex);
void mutex_unlock(mutex_t *mutex);
struct mutex_guard { mutex_t *mutex; };
static inline struct mutex_guard mutex_scope_enter(mutex_t *mutex){
    mutex_lock(mutex);
    return (struct mutex_guard){mutex};
}
static inline void mutex_scope_leave(struct mutex_guard *guard){ mutex_unlock(guard->mutex); }
#define MUTEX_SCOPE(m) struct mutex_guard mutex_scope_guard \
    __attribute__((cleanup(mutex_scope_leave))) = mutex_scope_enter(m)
