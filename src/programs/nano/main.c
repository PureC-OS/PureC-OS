#include "editor.h"
#include "../../libc/include/purec.h"

#define NANO_PATH_CAPACITY 128

void _start(void){
    char arguments[NANO_PATH_CAPACITY];
    if(pc_get_command_line(arguments,sizeof(arguments))<=0){
        pc_write("usage: /bin/program/nano <file>\n");
        pc_exit(2);
    }
    const char *argument=arguments;
    while(*argument==' ' || *argument=='\t') argument++;
    uint32_t argument_length=0;
    while(argument[argument_length]
          && argument[argument_length]!=' ' && argument[argument_length]!='\t')
        argument_length++;
    const char *remaining=&argument[argument_length];
    while(*remaining==' ' || *remaining=='\t') remaining++;
    if(!argument_length || *remaining){
        pc_write("nano: exactly one file path is required\n");
        pc_exit(2);
    }
    char path[NANO_PATH_CAPACITY];
    uint32_t path_length=0;
    if(argument[0]!='/'){
        char directory[NANO_PATH_CAPACITY];
        if(pc_getenv("PWD",directory,sizeof(directory))<0)
            pc_copy(directory,"/",sizeof(directory));
        path_length=pc_strlen(directory);
        if(path_length+argument_length+2>sizeof(path)){
            pc_write("nano: file path is too long\n");
            pc_exit(2);
        }
        pc_copy(path,directory,sizeof(path));
        if(path_length>1) path[path_length++]='/';
    } else if(argument_length+1>sizeof(path)){
        pc_write("nano: file path is too long\n");
        pc_exit(2);
    }
    for(uint32_t index=0;index<argument_length;index++)
        path[path_length++]=argument[index];
    path[path_length]='\0';
    pc_exit(nano_run(path));
}
