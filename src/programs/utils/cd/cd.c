#include "../lib/common.h"

int main(int argc, char **argv)
{
    util_init(argc, argv);
    const char *target = argc > 1 ? argv[argc - 1] : getenv("HOME");
    if (!target) target = "/";
    if (!util_is_dir(target)) return util_err("%s: %s", target, util_exists(target) ? "Not a directory" : "No such file or directory");
    return 0;
}
