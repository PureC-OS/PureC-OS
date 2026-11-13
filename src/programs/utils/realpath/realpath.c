#include "../lib/common.h"
#include <limits.h>

int main(int argc, char **argv)
{
    util_init(argc, argv);
    struct opts o;
    opts_begin(&o, argc, argv);
    bool missing_ok = false, quiet = false;
    int c;
    while ((c = opts_next(&o, "emqsz")) != -1) {
        if (c == 'm') missing_ok = true;
        else if (c == 'q') quiet = true;
        else if (c == 'e' || c == 's' || c == 'z') continue;
        else if (c == 'L' && opts_long(&o, "help")) { util_usage("[-mq] FILE..."); return 0; }
        else return util_bad_option(&o, c);
    }
    if (!o.count) return util_err("missing operand");
    int rc = 0;
    for (int i = 0; i < o.count; i++) {
        char out[UTIL_PATH];
        if (realpath(o.operands[i], out)) { puts(out); continue; }
        if (missing_ok) {
            char cwd[UTIL_PATH];
            if (o.operands[i][0] == '/') puts(o.operands[i]);
            else if (getcwd(cwd, sizeof(cwd))) {
                char joined[UTIL_PATH];
                util_join(joined, sizeof(joined), cwd, o.operands[i]);
                puts(joined);
            }
            continue;
        }
        if (!quiet) util_perr(o.operands[i]);
        rc = 1;
    }
    return rc;
}
