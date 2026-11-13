#include "../lib/common.h"

int main(int argc, char **argv)
{
    util_init(argc, argv);
    bool physical = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-P")) physical = true;
        else if (!strcmp(argv[i], "-L")) physical = false;
        else if (!strcmp(argv[i], "--help")) { util_usage("[-LP]"); return 0; }
        else return util_err("invalid option '%s'", argv[i]);
    }
    const char *env = getenv("PWD");
    if (!physical && env && env[0] == '/') {
        puts(env);
        return 0;
    }
    char buf[UTIL_PATH];
    if (!getcwd(buf, sizeof(buf))) return util_perr("getcwd");
    puts(buf);
    return 0;
}
