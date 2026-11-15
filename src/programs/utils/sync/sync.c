#include "../lib/common.h"
#include "libc/include/purec.h"

int main(int argc, char **argv)
{
    util_init(argc, argv);
    if (argc > 1) {
        if (!strcmp(argv[1], "--help")) {
            util_usage("");
            return 0;
        }
        return util_err("unexpected operand '%s'", argv[1]);
    }
    if (pc_sync() < 0)
        return util_err("cannot write the cached data to the disk");
    return 0;
}
