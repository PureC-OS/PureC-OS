#include "persistent_log.h"
#include "klog.h"
#include "../process/scheduler.h"
#include "../syscall/syscall.h"
#include "../../drivers/interrupts/timer.h"
#include "../../fs/vfs.h"

#define PERSISTENT_LOG_CHUNK (64U * 1024U)
#define PERSISTENT_LOG_MAX_BYTES 0xFFFFFFFFULL
#define PERSISTENT_LOG_PATH "/kernel.log"

static char persistent_log_buffer[PERSISTENT_LOG_CHUNK];

void persistent_log_thread(void *argument){
    (void)argument;
    uint64_t cursor=0;
    uint64_t file_size=0;
    uint32_t pending=0;
    uint64_t last_flush_tick=timer_ticks();

    filesystem_syscall_lock();
    int32_t clear_result=vfs_write_file(PERSISTENT_LOG_PATH,0,0);
    filesystem_syscall_unlock();
    if(clear_result<0){
        klogf(KLOG_ERROR,
              "klog-disk: cannot create %s status=%d; persistent logging disabled",
              PERSISTENT_LOG_PATH,clear_result);
        scheduler_exit();
    }

    klogf(KLOG_OK,
          "klog-disk: streaming enabled path=%s chunk=%u max_bytes=%llu ram_ring=%u",
          PERSISTENT_LOG_PATH,PERSISTENT_LOG_CHUNK,
          PERSISTENT_LOG_MAX_BYTES,8U*1024U*1024U);
    for(;;){
        bool data_lost=false;
        uint32_t amount=klog_read_since(
            &cursor,persistent_log_buffer+pending,
            sizeof(persistent_log_buffer)-pending,&data_lost);
        pending+=amount;
        if(data_lost)
            klogf(KLOG_ERROR,
                  "klog-disk: RAM ring overrun cursor advanced to=%llu total=%llu",
                  cursor,klog_total_bytes());
        uint64_t now=timer_ticks();
        if(!pending || (pending<sizeof(persistent_log_buffer)
                        && now-last_flush_tick<1000)){
            scheduler_sleep(20);
            continue;
        }
        uint64_t remaining=PERSISTENT_LOG_MAX_BYTES-file_size;
        amount=pending;
        if(amount>remaining) amount=(uint32_t)remaining;
        if(!amount){
            klogf(KLOG_WARN,"klog-disk: file limit reached path=%s",
                  PERSISTENT_LOG_PATH);
            scheduler_exit();
        }
        filesystem_syscall_lock();
        int32_t result=vfs_append_file(PERSISTENT_LOG_PATH,
                                       persistent_log_buffer,amount);
        filesystem_syscall_unlock();
        if(result<0 || (uint32_t)result!=amount){
            klogf(KLOG_ERROR,
                  "klog-disk: append failed status=%d requested=%u file_size=%llu",
                  result,amount,file_size);
            scheduler_exit();
        }
        file_size+=(uint32_t)result;
        pending=0;
        last_flush_tick=now;
        scheduler_sleep(20);
    }
}
