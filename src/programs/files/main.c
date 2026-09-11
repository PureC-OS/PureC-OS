#include "app.h"
#include "../../libc/include/purec.h"

void _start(void){
    pc_exit(files_app_run());
}
