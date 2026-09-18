#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef struct { unsigned value; } spinlock_t;
#define SPINLOCK_INIT {0}

static inline uint64_t irq_save(void){
    uint64_t flags;
    __asm__ volatile("pushfq; pop %0; cli" : "=r"(flags) :: "memory");
    return flags;
}
static inline void irq_restore(uint64_t flags){
    if(flags & (1ULL << 9)) __asm__ volatile("sti" ::: "memory");
}
static inline void spin_lock(spinlock_t *lock){
    for(;;){
        if(!__atomic_exchange_n(&lock->value, 1, __ATOMIC_ACQUIRE)) return;
        while(__atomic_load_n(&lock->value, __ATOMIC_RELAXED))
            __asm__ volatile("pause" ::: "memory");
    }
}
static inline void spin_unlock(spinlock_t *lock){
    __atomic_store_n(&lock->value, 0, __ATOMIC_RELEASE);
}
static inline uint64_t spin_lock_irqsave(spinlock_t *lock){
    uint64_t flags = irq_save();
    spin_lock(lock);
    return flags;
}
static inline void spin_unlock_irqrestore(spinlock_t *lock, uint64_t flags){
    spin_unlock(lock);
    irq_restore(flags);
}
