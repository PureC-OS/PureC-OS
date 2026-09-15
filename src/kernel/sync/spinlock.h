#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    volatile uint32_t word;
} spinlock_t;

static inline void cpu_relax(void){
    __asm__ volatile("pause" ::: "memory");
}

static inline uint64_t irq_save(void){
    uint64_t flags;
    __asm__ volatile("pushfq; pop %0; cli" : "=r"(flags) :: "memory");
    return flags;
}

static inline void irq_restore(uint64_t flags){
    if(flags & (1ULL << 9)) __asm__ volatile("sti" ::: "memory");
}

static inline void spin_lock(spinlock_t *lock){
    while(__atomic_test_and_set(&lock->word, __ATOMIC_ACQUIRE)){
        while(__atomic_load_n(&lock->word, __ATOMIC_RELAXED))
            cpu_relax();
    }
}

static inline void spin_unlock(spinlock_t *lock){
    __atomic_clear(&lock->word, __ATOMIC_RELEASE);
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
