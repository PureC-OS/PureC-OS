#include "../../libc/include/purec.h"

void _start(void){
    int64_t rc=pc_syscall(SYS_GUI_START,0,0,0);
    if(rc<0){
        pc_write("startgui: cannot start the graphical session\n");
        pc_exit(1);
    }
    pc_exit(0);
}
