#include "../../libc/include/purec.h"

#define INIT_REAP_CAPACITY 64
#define SERVICE_RESTART_DELAY_MS 250

struct init_service {
    const char *path;
    int32_t pid;
    bool critical;
};

static struct init_service services[] = {
    {"/bin/window-manager", -1, true},
    {"/bin/program/login", -1, false},
};

static void start_service(struct init_service *service){
    service->pid=pc_exec(service->path);
    pc_write("init: start ");
    pc_write(service->path);
    pc_write(" pid=");
    pc_write_i64(service->pid);
    pc_write("\n");
}

void _start(void){
    pc_write("init: PID 1 started\n");
    start_service(&services[0]);
    /* Give the WM time to claim the display before GUI clients register. */
    pc_sleep(50);
    start_service(&services[1]);
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
                    for(uint32_t s=0;s<sizeof(services)/sizeof(services[0]);s++){
                        if(services[s].pid!=(int32_t)list[i].pid) continue;
                        services[s].pid=-1;
                        pc_write("init: service exited ");
                        pc_write(services[s].path);
                        pc_write(" status=");
                        pc_write_i64(status);
                        pc_write("\n");
                    }
                }
            }
        }
        for(uint32_t s=0;s<sizeof(services)/sizeof(services[0]);s++){
            if(services[s].critical && services[s].pid<0){
                pc_sleep(SERVICE_RESTART_DELAY_MS);
                start_service(&services[s]);
            }
        }
        pc_sleep(500);
    }
}
