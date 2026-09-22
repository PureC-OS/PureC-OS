#include "../../libc/include/purec.h"

#define INIT_REAP_CAPACITY 64

void _start(void){
    pc_write("init: PID 1 started\n");
    static struct process_monitor_info list[INIT_REAP_CAPACITY];
    for(;;){
        int32_t count=pc_process_list(list,INIT_REAP_CAPACITY);
        if(count>INIT_REAP_CAPACITY) count=INIT_REAP_CAPACITY;
        if(count>0){
            for(int32_t i=0;i<count;i++){
                if(list[i].parent_pid==1
                   && list[i].state==PROCESS_MONITOR_STATE_EXITED){
                    int32_t status=0;
                    (void)pc_wait((int32_t)list[i].pid,&status,true);
                }
            }
        }
        pc_sleep(500);
    }
}
