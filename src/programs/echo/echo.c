#include "../../libc/include/purec.h"

#define ECHO_ARG_CAPACITY 512

int echo_main(void){
    char arguments[ECHO_ARG_CAPACITY];
    if(pc_get_command_line(arguments, sizeof(arguments)) < 0){
        arguments[0] = '\0';
    }
    pc_write(arguments);
    pc_write("\n");
    return 0;
}

void _start(void){
    pc_exit(echo_main());
}