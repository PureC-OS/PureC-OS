#include "../lib/common.h"
#include "libc/include/purec.h"

int main(int argc, char **argv)
{
    util_init(argc, argv);
    if (argc == 2 && !strcmp(argv[1], "--help")) {
        util_usage("N");
        return 0;
    }
    if (argc != 2) return util_err("usage: chvt N (1-6 text, 7 graphics)");
    char *end;
    long n = strtol(argv[1], &end, 10);
    if (*end || n < 1 || n > 7) return util_err("invalid console '%s'", argv[1]);
    int64_t rc = pc_syscall(SYS_VT_ACTIVATE, (uint64_t)(n - 1), 0, 0);
    if (rc == -1 || rc == -13) return util_err("permission denied");
    if (rc < 0) return util_err("cannot switch to console %ld", n);
    return 0;
}
