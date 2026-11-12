#include "../../libc/include/purec.h"

void _start(void){
    pc_write("\x1b[H\x1b[2J\x1b[3J");
    pc_exit(0);
}
