#include "../../libc/include/purec.h"
void _start(void){
    struct cpu_monitor_info info;
    uint64_t start=0;
    if(pc_cpu_info(&info)) start=info.uptime_ms;
    pc_write("burn: spinning 45s\n");
    volatile unsigned long long acc=0;
    for(;;){
        for(unsigned long long i=0;i<2000000ULL;i++) acc+=i*2654435761ULL;
        if(pc_cpu_info(&info) && info.uptime_ms-start>=45000) break;
    }
    pc_write("burn: done ");
    pc_write_u64((uint64_t)acc);
    pc_write("\n");
    pc_exit(0);
}
