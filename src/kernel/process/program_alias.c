#include "program_alias.h"
#include "../../lib/string.h"

static const char *system_program_names[]={
    "ls",
    "cat",
    "touch",
    "mkdir",
    "disks",
    "usbscan",
    "mkfs.fat32",
    "install",
    "setup",
    "update",
    "dmesg",
    "savelog",
    "uname",
    "about",
    "systeminfo",
    "htop",
    "font",
    "snake",
    "mouse",
    "debug",
    "reboot",
    "poweroff",
    "shutdown",
    "battery",
    "halt"
};

bool program_alias_resolve(const char *requested_path, const char **module_path){
    if(!requested_path || !module_path) return false;
    const char *name=requested_path;
    bool has_slash=false;
    for(const char *cursor=requested_path;*cursor;cursor++){
        if(*cursor=='/'){
            has_slash=true;
            if(cursor[1]) name=cursor+1;
        }
    }
    if(!name[0]) return false;
    bool prefix_ok=false;
    if(strncmp(requested_path,"/bin/program/",13)==0) prefix_ok=true;
    else if(strncmp(requested_path,"/bin/",5)==0) prefix_ok=true;
    else if(!has_slash) prefix_ok=true;
    if(!prefix_ok) return false;
    for(uint32_t index=0;
        index<sizeof(system_program_names)/sizeof(system_program_names[0]);
        index++){
        if(strcmp(name,system_program_names[index])==0){
            *module_path="/bin/program/system";
            return true;
        }
    }
    return false;
}
