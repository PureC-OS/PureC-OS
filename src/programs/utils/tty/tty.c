#include "../lib/common.h"
#include "libc/include/purec.h"

int main(int argc, char **argv)
{
    util_init(argc, argv);
    bool silent = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-s") || !strcmp(argv[i], "--silent") || !strcmp(argv[i], "--quiet")) silent = true;
        else if (!strcmp(argv[i], "--help")) {
            util_usage("[-s]");
            return 0;
        } else return util_err("invalid argument '%s'", argv[i]);
    }
    const char *name = getenv("TTY");
    bool term = isatty(0);
    if (!term) {
        if (!silent) printf("not a tty\n");
        return 1;
    }
    if (!silent) {
        if (name && name[0]) printf("/dev/%s\n", name);
        else printf("/dev/pts\n");
    }
    return 0;
}
