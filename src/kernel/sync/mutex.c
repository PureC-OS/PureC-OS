#include "mutex.h"
#include "../process/scheduler.h"
#include "spinlock.h"
#include <stddef.h>
#include <stdint.h>

void mutex_init(struct mutex *mutex){
    if(!mutex) return;
    mutex->guard.word = 0;
    mutex->locked = 0;
    mutex->owner = -1;
    mutex->head = NULL;
    mutex->tail = NULL;
}

void mutex_lock(struct mutex *mutex){
    if(!mutex) return;
    int self = scheduler_current_tid();
    uint64_t flags = spin_lock_irqsave(&mutex->guard);
    if(!mutex->locked){
        mutex->locked = 1;
        mutex->owner = self;
        spin_unlock_irqrestore(&mutex->guard, flags);
        return;
    }
    struct thread *waiter = scheduler_current_thread();
    waiter->wait_next = NULL;
    waiter->wait_woken = false;
    if(mutex->tail) mutex->tail->wait_next = waiter;
    else mutex->head = waiter;
    mutex->tail = waiter;
    spin_unlock_irqrestore(&mutex->guard, flags);
    scheduler_wait_queued();
}

bool mutex_try_lock(struct mutex *mutex){
    if(!mutex) return false;
    uint64_t flags = spin_lock_irqsave(&mutex->guard);
    if(mutex->locked){
        spin_unlock_irqrestore(&mutex->guard, flags);
        return false;
    }
    mutex->locked = 1;
    mutex->owner = scheduler_current_tid();
    spin_unlock_irqrestore(&mutex->guard, flags);
    return true;
}

void mutex_unlock(struct mutex *mutex){
    if(!mutex) return;
    uint64_t flags = spin_lock_irqsave(&mutex->guard);
    if(!mutex->locked){
        spin_unlock_irqrestore(&mutex->guard, flags);
        return;
    }
    if(mutex->head){
        struct thread *waiter = mutex->head;
        mutex->head = waiter->wait_next;
        if(!mutex->head) mutex->tail = NULL;
        waiter->wait_next = NULL;
        waiter->wait_woken = true;
        mutex->owner = (int32_t)waiter->id;
        scheduler_make_ready(waiter);
    } else {
        mutex->locked = 0;
        mutex->owner = -1;
    }
    spin_unlock_irqrestore(&mutex->guard, flags);
}
