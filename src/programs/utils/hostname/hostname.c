#include "../lib/common.h"

int main(int argc, char **argv)
{
    util_init(argc, argv);
    if (argc > 1 && !strcmp(argv[1], "--help")) { util_usage("[-s]"); return 0; }
    char buf[256];
    if (gethostname(buf, sizeof(buf)) != 0) return util_perr("gethostname");
    if (argc > 1 && !strcmp(argv[1], "-s")) {
        char *dot = strchr(buf, '.');
        if (dot) *dot = 0;
    }
    puts(buf);
    return 0;
}
