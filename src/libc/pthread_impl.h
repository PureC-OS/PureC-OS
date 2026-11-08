#pragma once

#include <stddef.h>
#include <stdint.h>

#define PC_FUTEX_WAIT 0
#define PC_FUTEX_WAKE 1
#define PC_FUTEX_NO_TIMEOUT 0xFFFFFFFFu

#define PC_PTHREAD_KEYS_MAX 128
#define PC_PTHREAD_DESTRUCTOR_ROUNDS 4
#define PC_THREAD_STACK_DEFAULT (1024u * 1024u)
#define PC_THREAD_STACK_MIN (16u * 1024u)
#define PC_THREAD_GUARD_SIZE 4096u

#define PC_DETACH_JOINABLE 0
#define PC_DETACH_DETACHED 1
#define PC_DETACH_EXITING 2

struct pc_thread_atexit
{
    struct pc_thread_atexit *next;
    void (*function)(void *);
    void *argument;
};

struct pthread
{
    struct pthread *self;
    void *header_pad[4];
    uintptr_t stack_guard;
    volatile uint32_t tid_word;
    int32_t ktid;
    volatile int detach_state;
    int reserved;
    void *(*start)(void *);
    void *argument;
    void *result;
    void *map_base;
    size_t map_size;
    int error_number;
    int reserved2;
    void *stack_addr;
    size_t stack_size;
    struct pc_thread_atexit *atexit_list;
    void *keys[PC_PTHREAD_KEYS_MAX];
};

struct pc_rlock
{
    volatile int word;
    volatile uintptr_t owner;
    int depth;
};

extern int __pc_threaded;

int64_t __pc_futex(int operation, volatile void *address, uint32_t value,
                   uint32_t timeout_ms);
struct pthread *__pc_self(void);
void __pc_rlock_lock(struct pc_rlock *lock);
void __pc_rlock_unlock(struct pc_rlock *lock);
void __tls_init_main(const void *image, size_t filesz, size_t memsz,
                     size_t alignment);
