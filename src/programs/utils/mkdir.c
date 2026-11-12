#include "fileops.h"

int main(int argc, char **argv)
{
    util_init(argc, argv);
    struct opts o;
    opts_begin(&o, argc, argv);
    bool parents = false, verbose = false;
    unsigned int mode = 0755;
    int c;
    while ((c = opts_next(&o, "pvm:")) != -1) {
        switch (c) {
        case 'p': parents = true; break;
        case 'v': verbose = true; break;
        case 'm': if (!util_parse_mode(o.arg, &mode, true)) return util_err("invalid mode '%s'", o.arg); break;
        case 'L':
            if (opts_long(&o, "parents")) { parents = true; break; }
            if (opts_long(&o, "help")) { util_usage("[-pv] [-m MODE] DIRECTORY..."); return 0; }
            return util_bad_option(&o, c);
        default: return util_bad_option(&o, c);
        }
    }
    if (!o.count) return util_err("missing operand");
    int rc = 0;
    for (int i = 0; i < o.count; i++) {
        if (parents) {
            rc |= fo_mkdirs(o.operands[i], mode, verbose);
            continue;
        }
        if (mkdir(o.operands[i], mode) != 0) {
            rc = util_err("cannot create directory '%s': %s", o.operands[i], strerror(errno));
            continue;
        }
        if (verbose) printf("%s: created directory '%s'\n", prog, o.operands[i]);
    }
    return rc;
}
