#ifndef PUREC_HOSTED_SEMAPHORE_H
#define PUREC_HOSTED_SEMAPHORE_H

#include "time.h"

typedef struct { int value; int waiters; } sem_t;

#define SEM_VALUE_MAX 2147483647

#ifdef __cplusplus
extern "C" {
#endif

int sem_init(sem_t *semaphore, int shared, unsigned int value);
int sem_destroy(sem_t *semaphore);
int sem_wait(sem_t *semaphore);
int sem_trywait(sem_t *semaphore);
int sem_timedwait(sem_t *semaphore, const struct timespec *deadline);
int sem_post(sem_t *semaphore);
int sem_getvalue(sem_t *semaphore, int *value);

#ifdef __cplusplus
}
#endif

#endif
