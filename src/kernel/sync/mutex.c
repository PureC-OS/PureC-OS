#include "mutex.h"
#include "../process/scheduler.h"
#include "../diagnostics/panic.h"
#include "../../arch/x86_64/gdt/include/gdt.h"

static uint64_t caller(void){
    int tid=scheduler_current_tid();
    return tid>0 ? (uint64_t)tid : UINT64_MAX-gdt_current_cpu_id();
}
void mutex_lock(mutex_t *mutex){
    uint64_t owner=caller();
    for(;;){
        uint64_t flags=spin_lock_irqsave(&mutex->lock);
        if(!mutex->owner || mutex->owner==owner){
            mutex->owner=owner;
            mutex->depth++;
            spin_unlock_irqrestore(&mutex->lock,flags);
            return;
        }
        spin_unlock_irqrestore(&mutex->lock,flags);
        if(scheduler_is_running()) scheduler_sleep(1);
        else __asm__ volatile("pause");
    }
}
void mutex_unlock(mutex_t *mutex){
    uint64_t flags=spin_lock_irqsave(&mutex->lock);
    if(mutex->owner!=caller() || !mutex->depth) kernel_panic("mutex: invalid owner");
    if(--mutex->depth==0) mutex->owner=0;
    spin_unlock_irqrestore(&mutex->lock,flags);
}
