#include "../../libc/include/purec.h"

#define INIT_REAP_CAPACITY 64

void _start(void){
    pc_write("init: PID 1 started\n");
    static struct process_monitor_info list[INIT_REAP_CAPACITY];
    for(;;){
        uint32_t offset=0;
        for(;;){
            int32_t total=pc_process_list_page(list,INIT_REAP_CAPACITY,offset);
            if(total<=0) break;
            int32_t got=total-(int32_t)offset;
            if(got>INIT_REAP_CAPACITY) got=INIT_REAP_CAPACITY;
            if(got<=0) break;
            for(int32_t i=0;i<got;i++){
                if(list[i].parent_pid==1
                   && list[i].state==PROCESS_MONITOR_STATE_EXITED){
                    int32_t status=0;
                    (void)pc_wait((int32_t)list[i].pid,&status,true);
                }
            }
            offset+=(uint32_t)got;
            if(offset>=(uint32_t)total) break;
        }
        pc_sleep(500);
    }
}
