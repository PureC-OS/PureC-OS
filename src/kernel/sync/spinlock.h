#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <arch/arch.h>

typedef struct {
  unsigned value;
} spinlock_t;
#define SPINLOCK_INIT {0}

static inline uint64_t irq_save(void) {
  return arch_irq_save();
}
static inline void irq_restore(uint64_t flags) {
  arch_irq_restore(flags);
}
static inline void spin_lock(spinlock_t *lock) {
  for (;;) {
    if (!__atomic_exchange_n(&lock->value, 1, __ATOMIC_ACQUIRE))
      return;
    while (__atomic_load_n(&lock->value, __ATOMIC_RELAXED))
      arch_cpu_relax();
  }
}
static inline void spin_unlock(spinlock_t *lock) {
  __atomic_store_n(&lock->value, 0, __ATOMIC_RELEASE);
}
static inline uint64_t spin_lock_irqsave(spinlock_t *lock) {
  uint64_t flags = irq_save();
  spin_lock(lock);
  return flags;
}
static inline void spin_unlock_irqrestore(spinlock_t *lock, uint64_t flags) {
  spin_unlock(lock);
  irq_restore(flags);
}
