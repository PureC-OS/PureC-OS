#include "commands.h"
#include "filesystem.h"
#include "system.h"
#include "../../libc/include/purec.h"

int system_command_run(const char *name, const char *arguments){
    int status=system_filesystem_command(name,arguments);
    if(status>=0) return status;
    status=system_platform_command(name,arguments);
    if(status>=0) return status;
    pc_write(name);
    pc_write(": unsupported system program\n");
    return 127;
}
