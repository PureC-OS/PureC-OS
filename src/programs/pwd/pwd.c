#include "../../libc/include/purec.h"

#define PWD_CAPACITY 256

int pwd_main(void){
    char dir[PWD_CAPACITY];
    if(pc_getenv("PWD", dir, sizeof(dir)) < 0 || !dir[0]){
        pc_copy(dir, "/", sizeof(dir));
    }
    pc_write(dir);
    pc_write("\n");
    return 0;
}

void _start(void){
    pc_exit(pwd_main());
}
