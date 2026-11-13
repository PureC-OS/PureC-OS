#include "../lib/common.h"
#include "libc/include/purec.h"

int main(int argc, char **argv)
{
    util_init(argc, argv);
    bool halt = !strcmp(prog, "halt"), off = !strcmp(prog, "poweroff") || !strcmp(prog, "shutdown") || halt;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-r")) off = false;
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "-P")) off = true;
        else if (!strcmp(argv[i], "now") || !strcmp(argv[i], "-f")) continue;
        else if (!strcmp(argv[i], "--help")) {
            util_usage(!strcmp(prog, "shutdown") ? "[-r|-h|-P] [now]" : "");
            return 0;
        } else return util_err("invalid argument '%s'", argv[i]);
    }
    if (getuid() != 0) return util_err("must be run as root");
    printf(off ? "System is powering off...\n" : "System is rebooting...\n");
    fflush(stdout);
    if (off) pc_shutdown();
    else pc_reboot();
    return util_err("operation failed");
}
