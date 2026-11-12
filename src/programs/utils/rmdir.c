#include "common.h"

int main(int argc, char **argv)
{
    util_init(argc, argv);
    struct opts o;
    opts_begin(&o, argc, argv);
    bool parents = false, verbose = false, ignore = false;
    int c;
    while ((c = opts_next(&o, "pv")) != -1) {
        if (c == 'p') parents = true;
        else if (c == 'v') verbose = true;
        else if (c == 'L' && opts_long(&o, "ignore-fail-on-non-empty")) ignore = true;
        else if (c == 'L' && opts_long(&o, "help")) { util_usage("[-pv] DIRECTORY..."); return 0; }
        else return util_bad_option(&o, c);
    }
    if (!o.count) return util_err("missing operand");
    int rc = 0;
    for (int i = 0; i < o.count; i++) {
        char path[UTIL_PATH];
        snprintf(path, sizeof(path), "%s", o.operands[i]);
        for (;;) {
            if (rmdir(path) != 0) {
                if (!ignore) rc = util_err("failed to remove '%s': %s", path, strerror(errno));
                break;
            }
            if (verbose) printf("%s: removing directory, '%s'\n", prog, path);
            if (!parents) break;
            char *slash = strrchr(path, '/');
            if (!slash || slash == path) break;
            *slash = 0;
        }
    }
    return rc;
}
