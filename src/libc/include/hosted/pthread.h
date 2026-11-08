#pragma once
#include <stddef.h>
#include "time.h"
#include "sched.h"

typedef unsigned long pthread_t;
typedef struct
{
    int locked;
    int kind;
    int owner;
    int count;
} pthread_mutex_t;
typedef struct
{
    int kind;
} pthread_mutexattr_t;
typedef struct
{
    size_t stack_size;
    size_t guard_size;
    int detach_state;
} pthread_attr_t;
typedef struct
{
    int clock;
} pthread_condattr_t;
typedef struct
{
    int seq;
    int waiters;
} pthread_cond_t;
typedef int pthread_once_t;
typedef unsigned int pthread_key_t;
typedef struct
{
    int state;
    int reserved;
} pthread_rwlock_t;
typedef struct
{
    int dummy;
} pthread_rwlockattr_t;

#define PTHREAD_MUTEX_INITIALIZER {0, 0, 0, 0}
#define PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP {0, 1, 0, 0}
#define PTHREAD_COND_INITIALIZER {0, 0}
#define PTHREAD_RWLOCK_INITIALIZER {0, 0}
#define PTHREAD_ONCE_INIT 0
#define PTHREAD_MUTEX_NORMAL 0
#define PTHREAD_MUTEX_RECURSIVE 1
#define PTHREAD_MUTEX_ERRORCHECK 2
#define PTHREAD_MUTEX_DEFAULT 0
#define PTHREAD_CREATE_JOINABLE 0
#define PTHREAD_CREATE_DETACHED 1
#define PTHREAD_STACK_MIN 16384
#define PTHREAD_KEYS_MAX 128
#define PTHREAD_DESTRUCTOR_ITERATIONS 4

#ifdef __cplusplus
extern "C"
{
#endif

    int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                       void *(*start)(void *), void *argument);
    int pthread_join(pthread_t thread, void **result);
    int pthread_detach(pthread_t thread);
    void pthread_exit(void *result) __attribute__((noreturn));
    pthread_t pthread_self(void);
    int pthread_equal(pthread_t a, pthread_t b);
    int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr);
    int pthread_mutex_destroy(pthread_mutex_t *mutex);
    int pthread_mutex_lock(pthread_mutex_t *mutex);
    int pthread_mutex_trylock(pthread_mutex_t *mutex);
    int pthread_mutex_unlock(pthread_mutex_t *mutex);
    int pthread_mutexattr_init(pthread_mutexattr_t *attr);
    int pthread_mutexattr_destroy(pthread_mutexattr_t *attr);
    int pthread_mutexattr_settype(pthread_mutexattr_t *attr, int kind);
    int pthread_mutexattr_gettype(const pthread_mutexattr_t *attr, int *kind);
    int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr);
    int pthread_cond_destroy(pthread_cond_t *cond);
    int pthread_cond_signal(pthread_cond_t *cond);
    int pthread_cond_broadcast(pthread_cond_t *cond);
    int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex);
    int pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                               const struct timespec *deadline);
    int pthread_condattr_init(pthread_condattr_t *attr);
    int pthread_condattr_destroy(pthread_condattr_t *attr);
    int pthread_condattr_setclock(pthread_condattr_t *attr, int clock);
    int pthread_once(pthread_once_t *once, void (*routine)(void));
    int pthread_key_create(pthread_key_t *key, void (*destructor)(void *));
    int pthread_key_delete(pthread_key_t key);
    void *pthread_getspecific(pthread_key_t key);
    int pthread_setspecific(pthread_key_t key, const void *value);
    int pthread_rwlock_init(pthread_rwlock_t *lock, const pthread_rwlockattr_t *attr);
    int pthread_rwlock_destroy(pthread_rwlock_t *lock);
    int pthread_rwlock_rdlock(pthread_rwlock_t *lock);
    int pthread_rwlock_tryrdlock(pthread_rwlock_t *lock);
    int pthread_rwlock_wrlock(pthread_rwlock_t *lock);
    int pthread_rwlock_trywrlock(pthread_rwlock_t *lock);
    int pthread_rwlock_unlock(pthread_rwlock_t *lock);
    int pthread_attr_init(pthread_attr_t *attr);
    int pthread_attr_destroy(pthread_attr_t *attr);
    int pthread_attr_setstacksize(pthread_attr_t *attr, size_t size);
    int pthread_attr_getstacksize(const pthread_attr_t *attr, size_t *size);
    int pthread_attr_setguardsize(pthread_attr_t *attr, size_t size);
    int pthread_attr_getguardsize(const pthread_attr_t *attr, size_t *size);
    int pthread_attr_setdetachstate(pthread_attr_t *attr, int state);
    int pthread_attr_getdetachstate(const pthread_attr_t *attr, int *state);
    int pthread_setname_np(pthread_t thread, const char *name);
    int pthread_getname_np(pthread_t thread, char *name, size_t size);
    
#ifdef __cplusplus
}
#endif
