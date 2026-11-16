#include <fcntl.h>
#include "../lib/common.h"
#include "libc/include/purec.h"

static bool ask_init(const char *verb)
{
    char req[64], tmp[64], ans[64];
    snprintf(req, sizeof(req), "/run/init/req/%d.req", (int)getpid());
    snprintf(tmp, sizeof(tmp), "/run/init/req/%d.new", (int)getpid());
    snprintf(ans, sizeof(ans), "/run/init/req/%d.ans", (int)getpid());
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return false;
    (void)write(fd, verb, strlen(verb));
    (void)write(fd, "\n", 1);
    close(fd);
    if (rename(tmp, req) < 0) { unlink(tmp); return false; }
    for (int i = 0; i < 40; i++) {
        usleep(50000);
        if (access(ans, F_OK) == 0) { unlink(ans); return true; }
    }
    unlink(req);
    return false;
}

int main(int argc, char **argv)
{
    util_init(argc, argv);
    bool force = false;
    bool halt = !strcmp(prog, "halt"), off = !strcmp(prog, "poweroff") || !strcmp(prog, "shutdown") || halt;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-r")) off = false;
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "-P")) off = true;
        else if (!strcmp(argv[i], "-f") || !strcmp(argv[i], "--force")) force = true;
        else if (!strcmp(argv[i], "now")) continue;
        else if (!strcmp(argv[i], "--help")) {
            util_usage(!strcmp(prog, "shutdown") ? "[-r|-h|-P] [now]" : "");
            return 0;
        } else return util_err("invalid argument '%s'", argv[i]);
    }
    if (getuid() != 0) return util_err("must be run as root");
    printf(off ? "System is powering off...\n" : "System is rebooting...\n");
    fflush(stdout);
    if (!force && ask_init(halt ? "halt" : off ? "poweroff" : "reboot")) {
        for (;;) pause();
    }
    if (off) pc_shutdown();
    else pc_reboot();
    return util_err("operation failed");
}
