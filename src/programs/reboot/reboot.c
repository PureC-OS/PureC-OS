#include "../../libc/include/purec.h"

void _start(void){
    pc_reboot();
    pc_exit(0);
}
