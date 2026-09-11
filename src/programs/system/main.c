#include "commands.h"
#include "../../libc/include/purec.h"

#define SYSTEM_ARGUMENT_CAPACITY 256

void _start(void){
    char name[32];
    char arguments[SYSTEM_ARGUMENT_CAPACITY];
    if(pc_get_process_name(name,sizeof(name))<0) pc_copy(name,"system",sizeof(name));
    if(pc_get_command_line(arguments,sizeof(arguments))<0) arguments[0]='\0';
    pc_exit(system_command_run(name,arguments));
}
