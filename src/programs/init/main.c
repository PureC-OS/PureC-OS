#include "../../libc/include/purec.h"

void _start(void){
    pc_write("init: PID 1 started\n");
    for(;;) pc_sleep(1000);
}
