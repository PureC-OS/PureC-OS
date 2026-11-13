#include "include/hosted/pthread.h"
#include "include/hosted/semaphore.h"
#include "include/hosted/errno.h"
#include "include/hosted/stdlib.h"
#include "include/hosted/string.h"
#include "include/hosted/sys/mman.h"
#include "include/purec.h"
#include "pthread_impl.h"
#include "include/hosted/signal.h"
#include "include/hosted/unistd.h"
#include <stdint.h>

#undef errno
extern int errno;
extern void stdio_prepare(void) __attribute__((weak));
int __pc_threaded;
static uintptr_t main_stack_top;

static int tls_ready;
static const void *tls_image;
static size_t tls_filesz;
static size_t tls_memsz;
static size_t tls_align = 1;
static volatile int live_threads;
static pthread_mutex_t key_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned char key_used[PC_PTHREAD_KEYS_MAX];
static void (*key_destructor[PC_PTHREAD_KEYS_MAX])(void *);
static int main_atexit_registered;

static int64_t sys4(uint64_t number, uint64_t a, uint64_t b, uint64_t c,
                    uint64_t d)
{
    int64_t result;
    __asm__ volatile("int $0x80"
                     : "=a"(result)
                     : "a"(number), "b"(a), "c"(b), "d"(c), "S"(d)
                     : "r10", "r8", "memory");
    return result;
}

static inline struct pthread *thread_pointer(void)
{
    struct pthread *self;
    __asm__ volatile("movq %%fs:0, %0" : "=r"(self));
    return self;
}

static size_t round_up(size_t value, size_t alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

int64_t __pc_futex(int operation, volatile void *address, uint32_t value,
                   uint32_t timeout_ms)
{
    return sys4(SYS_FUTEX, (uint64_t)operation, (uint64_t)(uintptr_t)address,
                value, timeout_ms);
}

static void futex_wait_forever(volatile int *word, int expected)
{
    __pc_futex(PC_FUTEX_WAIT, word, (uint32_t)expected, PC_FUTEX_NO_TIMEOUT);
}

static void futex_wake_all(volatile int *word)
{
    __pc_futex(PC_FUTEX_WAKE, word, 0x7FFFFFFF, 0);
}

static size_t tls_data_offset(void)
{
    size_t alignment = tls_align < 64 ? 64 : tls_align;
    return round_up(tls_memsz, alignment);
}

static size_t tls_block_size(void)
{
    return tls_data_offset() + round_up(sizeof(struct pthread), 64);
}

static struct pthread *tls_install(uint8_t *base)
{
    uint8_t *pointer = base + tls_data_offset();
    if (tls_memsz && tls_filesz)
    {
        size_t span = round_up(tls_memsz, tls_align ? tls_align : 1);
        memcpy(pointer - span, tls_image, tls_filesz);
    }
    struct pthread *thread = (struct pthread *)pointer;
    thread->self = thread;
    thread->stack_guard = 0x5C0FFEE5A17ULL;
    return thread;
}

void __tls_init_main(const void *image, size_t filesz, size_t memsz,
                     size_t alignment)
{
    if (tls_ready)
        return;
    tls_image = image;
    tls_filesz = filesz;
    tls_memsz = memsz;
    tls_align = alignment ? alignment : 1;
    size_t total = round_up(tls_block_size(), 4096);
    void *base = mmap(0, total, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED)
        return;
    struct pthread *thread = tls_install((uint8_t *)base);
    thread->tid_word = 1;
    thread->ktid = (int32_t)pc_syscall(SYS_GETTID, 0, 0, 0);
    thread->map_base = 0;
    if (pc_syscall(SYS_SET_FS_BASE, (uint64_t)(uintptr_t)thread, 0, 0) < 0)
        return;
    tls_ready = 1;
}

int *__errno_location(void)
{
    if (tls_ready)
        return &thread_pointer()->error_number;
    return &errno;
}

void __pc_note_stack_top(uintptr_t top)
{
    main_stack_top = top;
}

struct pthread *__pc_self(void)
{
    if (!tls_ready)
        __tls_init_main(0, 0, 0, 0);
    return thread_pointer();
}

void __pc_rlock_lock(struct pc_rlock *lock)
{
    uintptr_t me = (uintptr_t)thread_pointer();
    if (lock->owner == me)
    {
        lock->depth++;
        return;
    }
    int expected = 0;
    if (!__atomic_compare_exchange_n(&lock->word, &expected, 1, 0,
                                     __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
    {
        while (__atomic_exchange_n(&lock->word, 2, __ATOMIC_ACQUIRE) != 0)
            futex_wait_forever(&lock->word, 2);
    }
    lock->owner = me;
    lock->depth = 1;
}

void __pc_rlock_unlock(struct pc_rlock *lock)
{
    if (--lock->depth)
        return;
    lock->owner = 0;
    if (__atomic_exchange_n(&lock->word, 0, __ATOMIC_RELEASE) == 2)
        __pc_futex(PC_FUTEX_WAKE, &lock->word, 1, 0);
}

int sched_yield(void)
{
    pc_syscall(SYS_SCHED_YIELD, 0, 0, 0);
    return 0;
}

static void run_thread_atexit(struct pthread *thread)
{
    while (thread->atexit_list)
    {
        struct pc_thread_atexit *entry = thread->atexit_list;
        thread->atexit_list = entry->next;
        entry->function(entry->argument);
        free(entry);
    }
}

static void run_key_destructors(struct pthread *thread)
{
    for (int round = 0; round < PC_PTHREAD_DESTRUCTOR_ROUNDS; round++)
    {
        int any = 0;
        for (unsigned key = 0; key < PC_PTHREAD_KEYS_MAX; key++)
        {
            void *value = thread->keys[key];
            void (*destructor)(void *) = key_destructor[key];
            if (value && key_used[key] && destructor)
            {
                thread->keys[key] = 0;
                destructor(value);
                any = 1;
            }
        }
        if (!any)
            break;
    }
}

static void main_thread_atexit(void)
{
    struct pthread *thread = thread_pointer();
    run_thread_atexit(thread);
    run_key_destructors(thread);
}

int __cxa_thread_atexit_impl(void (*function)(void *), void *argument,
                             void *dso)
{
    (void)dso;
    struct pthread *thread = __pc_self();
    struct pc_thread_atexit *entry = malloc(sizeof(*entry));
    if (!entry)
        return -1;
    entry->function = function;
    entry->argument = argument;
    entry->next = thread->atexit_list;
    thread->atexit_list = entry;
    if (!thread->map_base &&
        !__atomic_exchange_n(&main_atexit_registered, 1, __ATOMIC_ACQ_REL))
        atexit(main_thread_atexit);
    return 0;
}

int __cxa_thread_atexit(void (*function)(void *), void *argument, void *dso)
{
    return __cxa_thread_atexit_impl(function, argument, dso);
}

void pthread_exit(void *result)
{
    struct pthread *thread = __pc_self();
    thread->result = result;
    run_thread_atexit(thread);
    run_key_destructors(thread);
    if (!thread->map_base)
    {
        for (;;)
        {
            int remaining = __atomic_load_n(&live_threads, __ATOMIC_ACQUIRE);
            if (remaining <= 0)
                break;
            futex_wait_forever(&live_threads, remaining);
        }
        exit(0);
    }
    int previous = PC_DETACH_JOINABLE;
    uint64_t unmap_base = 0;
    uint64_t unmap_size = 0;
    if (!__atomic_compare_exchange_n(&thread->detach_state, &previous,
                                     PC_DETACH_EXITING, 0, __ATOMIC_ACQ_REL,
                                     __ATOMIC_ACQUIRE))
    {
        unmap_base = (uint64_t)(uintptr_t)thread->map_base;
        unmap_size = thread->map_size;
    }
    __atomic_fetch_sub(&live_threads, 1, __ATOMIC_ACQ_REL);
    futex_wake_all(&live_threads);
    pc_syscall(SYS_THREAD_EXIT, unmap_base, unmap_size, 0);
    for (;;)
        __asm__ volatile("hlt");
}

static void thread_entry(struct pthread *thread) __attribute__((noreturn));

static void thread_entry(struct pthread *thread)
{
    thread->ktid = (int32_t)pc_syscall(SYS_GETTID, 0, 0, 0);
    void *result = thread->start(thread->argument);
    pthread_exit(result);
    __builtin_unreachable();
}

int pthread_create(pthread_t *out, const pthread_attr_t *attr,
                   void *(*start)(void *), void *argument)
{
    if (!out || !start)
        return EINVAL;
    __pc_self();
    if (!tls_ready)
        return EAGAIN;
    if (stdio_prepare)
        stdio_prepare();
    size_t stack_size = PC_THREAD_STACK_DEFAULT;
    size_t guard_size = PC_THREAD_GUARD_SIZE;
    int detached = 0;
    if (attr)
    {
        if (attr->stack_size)
            stack_size = attr->stack_size;
        guard_size = attr->guard_size;
        detached = attr->detach_state == PTHREAD_CREATE_DETACHED;
    }
    if (stack_size < PC_THREAD_STACK_MIN)
        stack_size = PC_THREAD_STACK_MIN;
    stack_size = round_up(stack_size, 4096);
    guard_size = round_up(guard_size, 4096);
    size_t tls_size = round_up(tls_block_size(), 4096);
    size_t total = guard_size + stack_size + tls_size;
    uint8_t *base = mmap(0, total, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED)
        return EAGAIN;
    if (guard_size)
        mprotect(base, guard_size, PROT_NONE);
    uint8_t *tls_base = base + guard_size + stack_size;
    struct pthread *thread = tls_install(tls_base);
    thread->tid_word = 1;
    thread->start = start;
    thread->argument = argument;
    thread->map_base = base;
    thread->map_size = total;
    thread->stack_addr = base + guard_size;
    thread->stack_size = stack_size;
    thread->detach_state = detached ? PC_DETACH_DETACHED : PC_DETACH_JOINABLE;
    uint64_t *stack_top = (uint64_t *)tls_base;
    *--stack_top = 0;
    struct kernel_thread_args request;
    request.entry = (uint64_t)(uintptr_t)thread_entry;
    request.stack = (uint64_t)(uintptr_t)stack_top;
    request.argument = (uint64_t)(uintptr_t)thread;
    request.tls_base = (uint64_t)(uintptr_t)thread;
    request.clear_tid = (uint64_t)(uintptr_t)&thread->tid_word;
    __atomic_store_n(&__pc_threaded, 1, __ATOMIC_RELEASE);
    __atomic_fetch_add(&live_threads, 1, __ATOMIC_ACQ_REL);
    int64_t status = pc_syscall(SYS_THREAD_CREATE,
                                (uint64_t)(uintptr_t)&request, 0, 0);
    if (status < 0)
    {
        __atomic_fetch_sub(&live_threads, 1, __ATOMIC_ACQ_REL);
        munmap(base, total);
        return (int)-status;
    }
    *out = (pthread_t)(uintptr_t)thread;
    return 0;
}

int pthread_join(pthread_t handle, void **result)
{
    struct pthread *thread = (struct pthread *)(uintptr_t)handle;
    if (!thread)
        return ESRCH;
    if (thread == __pc_self())
        return EDEADLK;
    if (!thread->map_base || thread->detach_state == PC_DETACH_DETACHED)
        return EINVAL;
    for (;;)
    {
        uint32_t word = __atomic_load_n(&thread->tid_word, __ATOMIC_ACQUIRE);
        if (!word)
            break;
        __pc_futex(PC_FUTEX_WAIT, &thread->tid_word, word,
                   PC_FUTEX_NO_TIMEOUT);
    }
    if (result)
        *result = thread->result;
    munmap(thread->map_base, thread->map_size);
    return 0;
}

int pthread_detach(pthread_t handle)
{
    struct pthread *thread = (struct pthread *)(uintptr_t)handle;
    if (!thread || !thread->map_base)
        return EINVAL;
    int previous = PC_DETACH_JOINABLE;
    if (__atomic_compare_exchange_n(&thread->detach_state, &previous,
                                    PC_DETACH_DETACHED, 0, __ATOMIC_ACQ_REL,
                                    __ATOMIC_ACQUIRE))
        return 0;
    if (previous == PC_DETACH_EXITING)
        return pthread_join(handle, 0);
    return EINVAL;
}

pthread_t pthread_self(void)
{
    return (pthread_t)(uintptr_t)__pc_self();
}

int pthread_equal(pthread_t a, pthread_t b) { return a == b; }

int pthread_setname_np(pthread_t thread, const char *name)
{
    struct pthread *target = (struct pthread *)thread;
    if (!target || !name)
        return EINVAL;
    size_t length = 0;
    while (name[length])
        length++;
    if (length >= sizeof(target->name))
        return ERANGE;
    memcpy(target->name, name, length + 1);
    return 0;
}

int pthread_getname_np(pthread_t thread, char *name, size_t size)
{
    struct pthread *target = (struct pthread *)thread;
    if (!target || !name)
        return EINVAL;
    size_t length = 0;
    while (target->name[length])
        length++;
    if (length >= size)
        return ERANGE;
    memcpy(name, target->name, length + 1);
    return 0;
}

int pthread_attr_init(pthread_attr_t *attr)
{
    attr->stack_size = 0;
    attr->guard_size = PC_THREAD_GUARD_SIZE;
    attr->detach_state = PTHREAD_CREATE_JOINABLE;
    attr->stack_addr = 0;
    return 0;
}

int pthread_attr_destroy(pthread_attr_t *attr)
{
    (void)attr;
    return 0;
}

int pthread_attr_setstacksize(pthread_attr_t *attr, size_t size)
{
    if (size < PC_THREAD_STACK_MIN)
        return EINVAL;
    attr->stack_size = size;
    return 0;
}

int pthread_attr_getstacksize(const pthread_attr_t *attr, size_t *size)
{
    *size = attr->stack_size ? attr->stack_size : PC_THREAD_STACK_DEFAULT;
    return 0;
}

int pthread_attr_setguardsize(pthread_attr_t *attr, size_t size)
{
    attr->guard_size = size;
    return 0;
}

int pthread_attr_getguardsize(const pthread_attr_t *attr, size_t *size)
{
    *size = attr->guard_size;
    return 0;
}

int pthread_attr_setdetachstate(pthread_attr_t *attr, int state)
{
    if (state != PTHREAD_CREATE_JOINABLE && state != PTHREAD_CREATE_DETACHED)
        return EINVAL;
    attr->detach_state = state;
    return 0;
}

int pthread_attr_getdetachstate(const pthread_attr_t *attr, int *state)
{
    *state = attr->detach_state;
    return 0;
}

static int lock_word(volatile int *word)
{
    int expected = 0;
    if (__atomic_compare_exchange_n(word, &expected, 1, 0, __ATOMIC_ACQUIRE,
                                    __ATOMIC_RELAXED))
        return 0;
    while (__atomic_exchange_n(word, 2, __ATOMIC_ACQUIRE) != 0)
        futex_wait_forever(word, 2);
    return 0;
}

static void unlock_word(volatile int *word)
{
    if (__atomic_exchange_n(word, 0, __ATOMIC_RELEASE) == 2)
        __pc_futex(PC_FUTEX_WAKE, word, 1, 0);
}

int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr)
{
    mutex->locked = 0;
    mutex->kind = attr ? attr->kind : PTHREAD_MUTEX_NORMAL;
    mutex->owner = 0;
    mutex->count = 0;
    return 0;
}

int pthread_mutex_destroy(pthread_mutex_t *mutex)
{
    (void)mutex;
    return 0;
}

int pthread_mutex_lock(pthread_mutex_t *mutex)
{
    if (mutex->kind == PTHREAD_MUTEX_NORMAL)
        return lock_word(&mutex->locked);
    int me = __pc_self()->ktid;
    if (__atomic_load_n(&mutex->owner, __ATOMIC_ACQUIRE) == me)
    {
        if (mutex->kind == PTHREAD_MUTEX_RECURSIVE)
        {
            mutex->count++;
            return 0;
        }
        return EDEADLK;
    }
    lock_word(&mutex->locked);
    __atomic_store_n(&mutex->owner, me, __ATOMIC_RELEASE);
    mutex->count = 1;
    return 0;
}

int pthread_mutex_trylock(pthread_mutex_t *mutex)
{
    int expected = 0;
    if (mutex->kind == PTHREAD_MUTEX_NORMAL)
        return __atomic_compare_exchange_n(&mutex->locked, &expected, 1, 0,
                                           __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)
                   ? 0
                   : EBUSY;
    int me = __pc_self()->ktid;
    if (__atomic_load_n(&mutex->owner, __ATOMIC_ACQUIRE) == me &&
        mutex->kind == PTHREAD_MUTEX_RECURSIVE)
    {
        mutex->count++;
        return 0;
    }
    if (!__atomic_compare_exchange_n(&mutex->locked, &expected, 1, 0,
                                     __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
        return EBUSY;
    __atomic_store_n(&mutex->owner, me, __ATOMIC_RELEASE);
    mutex->count = 1;
    return 0;
}

int pthread_mutex_unlock(pthread_mutex_t *mutex)
{
    if (mutex->kind == PTHREAD_MUTEX_NORMAL)
    {
        unlock_word(&mutex->locked);
        return 0;
    }
    int me = __pc_self()->ktid;
    if (__atomic_load_n(&mutex->owner, __ATOMIC_ACQUIRE) != me)
        return EPERM;
    if (mutex->kind == PTHREAD_MUTEX_RECURSIVE && --mutex->count > 0)
        return 0;
    mutex->count = 0;
    __atomic_store_n(&mutex->owner, 0, __ATOMIC_RELEASE);
    unlock_word(&mutex->locked);
    return 0;
}

int pthread_mutexattr_init(pthread_mutexattr_t *attr)
{
    attr->kind = PTHREAD_MUTEX_NORMAL;
    return 0;
}

int pthread_mutexattr_destroy(pthread_mutexattr_t *attr)
{
    (void)attr;
    return 0;
}

int pthread_mutexattr_settype(pthread_mutexattr_t *attr, int kind)
{
    if (kind < PTHREAD_MUTEX_NORMAL || kind > PTHREAD_MUTEX_ERRORCHECK)
        return EINVAL;
    attr->kind = kind;
    return 0;
}

int pthread_mutexattr_gettype(const pthread_mutexattr_t *attr, int *kind)
{
    *kind = attr->kind;
    return 0;
}

int pthread_condattr_init(pthread_condattr_t *attr)
{
    attr->clock = CLOCK_REALTIME;
    return 0;
}

int pthread_condattr_destroy(pthread_condattr_t *attr)
{
    (void)attr;
    return 0;
}

int pthread_condattr_setclock(pthread_condattr_t *attr, int clock)
{
    attr->clock = clock;
    return 0;
}

#define COND_MONOTONIC_FLAG 0x40000000
#define COND_WAITER_MASK 0x3FFFFFFF

int pthread_condattr_getclock(const pthread_condattr_t *attr, int *clock)
{
    *clock = attr->clock;
    return 0;
}

int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr)
{
    cond->seq = 0;
    cond->waiters = (attr && attr->clock == CLOCK_MONOTONIC) ? COND_MONOTONIC_FLAG : 0;
    return 0;
}

int pthread_cond_destroy(pthread_cond_t *cond)
{
    (void)cond;
    return 0;
}

int pthread_cond_signal(pthread_cond_t *cond)
{
    if (!(__atomic_load_n(&cond->waiters, __ATOMIC_ACQUIRE) & COND_WAITER_MASK))
        return 0;
    __atomic_fetch_add(&cond->seq, 1, __ATOMIC_ACQ_REL);
    __pc_futex(PC_FUTEX_WAKE, &cond->seq, 1, 0);
    return 0;
}

int pthread_cond_broadcast(pthread_cond_t *cond)
{
    if (!(__atomic_load_n(&cond->waiters, __ATOMIC_ACQUIRE) & COND_WAITER_MASK))
        return 0;
    __atomic_fetch_add(&cond->seq, 1, __ATOMIC_ACQ_REL);
    futex_wake_all(&cond->seq);
    return 0;
}

static int cond_wait_ms(pthread_cond_t *cond, pthread_mutex_t *mutex,
                        uint32_t timeout_ms)
{
    int sequence = __atomic_load_n(&cond->seq, __ATOMIC_ACQUIRE);
    __atomic_fetch_add(&cond->waiters, 1, __ATOMIC_ACQ_REL);
    pthread_mutex_unlock(mutex);
    int64_t status =
        __pc_futex(PC_FUTEX_WAIT, &cond->seq, (uint32_t)sequence, timeout_ms);
    __atomic_fetch_sub(&cond->waiters, 1, __ATOMIC_ACQ_REL);
    pthread_mutex_lock(mutex);
    return status == -ETIMEDOUT ? ETIMEDOUT : 0;
}

int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
    return cond_wait_ms(cond, mutex, PC_FUTEX_NO_TIMEOUT);
}

int pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                           const struct timespec *deadline)
{
    struct timespec now;
    int clock = (__atomic_load_n(&cond->waiters, __ATOMIC_ACQUIRE) & COND_MONOTONIC_FLAG)
                    ? CLOCK_MONOTONIC
                    : CLOCK_REALTIME;
    if (clock_gettime(clock, &now) != 0)
        return EINVAL;
    long long remaining =
        ((long long)deadline->tv_sec - (long long)now.tv_sec) * 1000LL +
        ((long long)deadline->tv_nsec - (long long)now.tv_nsec + 999999LL) /
            1000000LL;
    if (remaining <= 0)
    {
        pthread_mutex_unlock(mutex);
        sched_yield();
        pthread_mutex_lock(mutex);
        return ETIMEDOUT;
    }
    if (remaining > 0xFFFFFFF0LL)
        remaining = 0xFFFFFFF0LL;
    return cond_wait_ms(cond, mutex, (uint32_t)remaining);
}

int pthread_once(pthread_once_t *once, void (*routine)(void))
{
    for (;;)
    {
        int state = __atomic_load_n(once, __ATOMIC_ACQUIRE);
        if (state == 2)
            return 0;
        int expected = 0;
        if (state == 0 &&
            __atomic_compare_exchange_n(once, &expected, 1, 0,
                                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        {
            routine();
            __atomic_store_n(once, 2, __ATOMIC_RELEASE);
            futex_wake_all(once);
            return 0;
        }
        if (state == 1)
            futex_wait_forever(once, 1);
    }
}

int pthread_key_create(pthread_key_t *key, void (*destructor)(void *))
{
    pthread_mutex_lock(&key_lock);
    for (unsigned i = 0; i < PC_PTHREAD_KEYS_MAX; i++)
    {
        if (!key_used[i])
        {
            key_used[i] = 1;
            key_destructor[i] = destructor;
            *key = i;
            pthread_mutex_unlock(&key_lock);
            return 0;
        }
    }
    pthread_mutex_unlock(&key_lock);
    return EAGAIN;
}

int pthread_key_delete(pthread_key_t key)
{
    if (key >= PC_PTHREAD_KEYS_MAX)
        return EINVAL;
    pthread_mutex_lock(&key_lock);
    key_used[key] = 0;
    key_destructor[key] = 0;
    pthread_mutex_unlock(&key_lock);
    return 0;
}

void *pthread_getspecific(pthread_key_t key)
{
    if (key >= PC_PTHREAD_KEYS_MAX || !key_used[key])
        return 0;
    return __pc_self()->keys[key];
}

int pthread_setspecific(pthread_key_t key, const void *value)
{
    if (key >= PC_PTHREAD_KEYS_MAX || !key_used[key])
        return EINVAL;
    __pc_self()->keys[key] = (void *)value;
    return 0;
}

int pthread_rwlock_init(pthread_rwlock_t *lock, const pthread_rwlockattr_t *attr)
{
    (void)attr;
    lock->state = 0;
    lock->reserved = 0;
    return 0;
}

int pthread_rwlock_destroy(pthread_rwlock_t *lock)
{
    (void)lock;
    return 0;
}

int pthread_rwlock_tryrdlock(pthread_rwlock_t *lock)
{
    int state = __atomic_load_n(&lock->state, __ATOMIC_ACQUIRE);
    while (state >= 0)
    {
        if (__atomic_compare_exchange_n(&lock->state, &state, state + 1, 0,
                                        __ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE))
            return 0;
    }
    return EBUSY;
}

int pthread_rwlock_rdlock(pthread_rwlock_t *lock)
{
    for (;;)
    {
        int state = __atomic_load_n(&lock->state, __ATOMIC_ACQUIRE);
        if (state >= 0)
        {
            if (__atomic_compare_exchange_n(&lock->state, &state, state + 1, 0,
                                            __ATOMIC_ACQUIRE,
                                            __ATOMIC_ACQUIRE))
                return 0;
            continue;
        }
        futex_wait_forever(&lock->state, state);
    }
}

int pthread_rwlock_trywrlock(pthread_rwlock_t *lock)
{
    int expected = 0;
    return __atomic_compare_exchange_n(&lock->state, &expected, -1, 0,
                                       __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)
               ? 0
               : EBUSY;
}

int pthread_rwlock_wrlock(pthread_rwlock_t *lock)
{
    for (;;)
    {
        int expected = 0;
        if (__atomic_compare_exchange_n(&lock->state, &expected, -1, 0,
                                        __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
            return 0;
        futex_wait_forever(&lock->state, expected);
    }
}

int pthread_rwlock_unlock(pthread_rwlock_t *lock)
{
    int state = __atomic_load_n(&lock->state, __ATOMIC_ACQUIRE);
    if (state == -1)
    {
        __atomic_store_n(&lock->state, 0, __ATOMIC_RELEASE);
        futex_wake_all(&lock->state);
        return 0;
    }
    if (state > 0 &&
        __atomic_sub_fetch(&lock->state, 1, __ATOMIC_RELEASE) == 0)
        futex_wake_all(&lock->state);
    return 0;
}

int pthread_attr_getstack(const pthread_attr_t *attr, void **address, size_t *size)
{
    *address = attr->stack_addr;
    *size = attr->stack_size ? attr->stack_size : PC_THREAD_STACK_DEFAULT;
    return 0;
}

int pthread_getattr_np(pthread_t handle, pthread_attr_t *attr)
{
    struct pthread *thread = (struct pthread *)(uintptr_t)handle;
    if (!thread || !attr)
        return EINVAL;
    pthread_attr_init(attr);
    if (thread->map_base)
    {
        attr->stack_addr = thread->stack_addr;
        attr->stack_size = thread->stack_size;
        attr->detach_state = thread->detach_state == PC_DETACH_DETACHED
                                 ? PTHREAD_CREATE_DETACHED
                                 : PTHREAD_CREATE_JOINABLE;
        return 0;
    }
    size_t size = 8u * 1024u * 1024u;
    attr->stack_size = size;
    attr->stack_addr = main_stack_top ? (void *)(main_stack_top - size) : 0;
    return 0;
}

int pthread_sigmask(int how, const sigset_t *set, sigset_t *previous)
{
    return sigprocmask(how, set, previous) == 0 ? 0 : errno;
}

int pthread_kill(pthread_t handle, int number)
{
    if (number == 0)
        return handle ? 0 : ESRCH;
    if (handle == pthread_self())
        return raise(number) == 0 ? 0 : errno;
    return kill(getpid(), number) == 0 ? 0 : errno;
}

int pthread_cancel(pthread_t handle)
{
    (void)handle;
    return ENOSYS;
}

int pthread_setcancelstate(int state, int *previous)
{
    if (previous)
        *previous = PTHREAD_CANCEL_ENABLE;
    (void)state;
    return 0;
}

int pthread_setcanceltype(int type, int *previous)
{
    if (previous)
        *previous = PTHREAD_CANCEL_DEFERRED;
    (void)type;
    return 0;
}

void pthread_testcancel(void) {}

int pthread_mutex_timedlock(pthread_mutex_t *mutex, const struct timespec *deadline)
{
    for (;;)
    {
        int status = pthread_mutex_trylock(mutex);
        if (status != EBUSY)
            return status;
        struct timespec now;
        if (clock_gettime(CLOCK_REALTIME, &now) != 0)
            return EINVAL;
        if (now.tv_sec > deadline->tv_sec ||
            (now.tv_sec == deadline->tv_sec && now.tv_nsec >= deadline->tv_nsec))
            return ETIMEDOUT;
        sched_yield();
    }
}

int sem_init(sem_t *semaphore, int shared, unsigned int value)
{
    (void)shared;
    if (value > SEM_VALUE_MAX)
    {
        errno = EINVAL;
        return -1;
    }
    semaphore->value = (int)value;
    semaphore->waiters = 0;
    return 0;
}

int sem_destroy(sem_t *semaphore)
{
    (void)semaphore;
    return 0;
}

int sem_trywait(sem_t *semaphore)
{
    int current = __atomic_load_n(&semaphore->value, __ATOMIC_ACQUIRE);
    while (current > 0)
    {
        if (__atomic_compare_exchange_n(&semaphore->value, &current, current - 1,
                                        0, __ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE))
            return 0;
    }
    errno = EAGAIN;
    return -1;
}

static int sem_wait_ms(sem_t *semaphore, uint32_t timeout_ms)
{
    for (;;)
    {
        int current = __atomic_load_n(&semaphore->value, __ATOMIC_ACQUIRE);
        if (current > 0)
        {
            if (__atomic_compare_exchange_n(&semaphore->value, &current,
                                            current - 1, 0, __ATOMIC_ACQUIRE,
                                            __ATOMIC_ACQUIRE))
                return 0;
            continue;
        }
        int64_t status =
            __pc_futex(PC_FUTEX_WAIT, &semaphore->value, 0, timeout_ms);
        if (status == -ETIMEDOUT)
        {
            errno = ETIMEDOUT;
            return -1;
        }
    }
}

int sem_wait(sem_t *semaphore)
{
    return sem_wait_ms(semaphore, PC_FUTEX_NO_TIMEOUT);
}

int sem_timedwait(sem_t *semaphore, const struct timespec *deadline)
{
    struct timespec now;
    if (clock_gettime(CLOCK_REALTIME, &now) != 0)
        return -1;
    long long remaining =
        ((long long)deadline->tv_sec - (long long)now.tv_sec) * 1000LL +
        ((long long)deadline->tv_nsec - (long long)now.tv_nsec + 999999LL) /
            1000000LL;
    if (remaining <= 0)
        return sem_trywait(semaphore) == 0 ? 0 : (errno = ETIMEDOUT, -1);
    if (remaining > 0xFFFFFFF0LL)
        remaining = 0xFFFFFFF0LL;
    return sem_wait_ms(semaphore, (uint32_t)remaining);
}

int sem_post(sem_t *semaphore)
{
    __atomic_fetch_add(&semaphore->value, 1, __ATOMIC_RELEASE);
    __pc_futex(PC_FUTEX_WAKE, &semaphore->value, 1, 0);
    return 0;
}

int sem_getvalue(sem_t *semaphore, int *value)
{
    *value = __atomic_load_n(&semaphore->value, __ATOMIC_ACQUIRE);
    return 0;
}
