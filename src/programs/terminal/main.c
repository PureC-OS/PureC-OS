#include "shell.h"
#include "window.h"
#include "../../libc/include/purec.h"

void _start(void){
    char initial_command[256];
    if(pc_get_command_line(initial_command,sizeof(initial_command))<0)
        initial_command[0]='\0';
    struct terminal_window terminal;
    if(!terminal_window_init(&terminal)) pc_exit(1);
    int status=shell_run(&terminal,initial_command);
    terminal_window_shutdown(&terminal);
    pc_exit(status);
}
