#include "../lib/fileops.h"

int main(int argc, char **argv)
{
    util_init(argc, argv);
    struct opts o;
    opts_begin(&o, argc, argv);
    bool dirs = false, make_parents = false, verbose = false;
    unsigned int mode = 0755;
    const char *target_dir = NULL;
    int c;
    while ((c = opts_next(&o, "dDvcCpsm:o:g:t:")) != -1) {
        switch (c) {
        case 'd': dirs = true; break;
        case 'D': make_parents = true; break;
        case 'v': verbose = true; break;
        case 'm': if (!util_parse_mode(o.arg, &mode, false)) return util_err("invalid mode '%s'", o.arg); break;
        case 't': target_dir = o.arg; break;
        case 'c': case 'C': case 'p': case 's': case 'o': case 'g': break;
        case 'L': if (opts_long(&o, "help")) { util_usage("[-dDv] [-m MODE] [-t DIR] SOURCE... DEST"); return 0; }
            return util_bad_option(&o, c);
        default: return util_bad_option(&o, c);
        }
    }
    int rc = 0;
    if (dirs) {
        for (int i = 0; i < o.count; i++) {
            rc |= fo_mkdirs(o.operands[i], mode, verbose);
            chmod(o.operands[i], mode);
        }
        return rc;
    }
    int nsrc = o.count;
    const char *dest = target_dir;
    if (!dest) {
        if (o.count < 2) return util_err("missing destination file operand");
        dest = o.operands[--nsrc];
    }
    if (make_parents) {
        char parent[UTIL_PATH];
        snprintf(parent, sizeof(parent), "%s", dest);
        if (!target_dir && nsrc == 1) {
            char *slash = strrchr(parent, '/');
            if (slash && slash != parent) { *slash = 0; fo_mkdirs(parent, 0755, verbose); }
        } else fo_mkdirs(parent, 0755, verbose);
    }
    bool dest_dir = util_is_dir(dest);
    for (int i = 0; i < nsrc; i++) {
        char target[UTIL_PATH];
        if (dest_dir) util_join(target, sizeof(target), dest, util_base(o.operands[i]));
        else snprintf(target, sizeof(target), "%s", dest);
        if (fo_copy_file(o.operands[i], target, false) != 0) { rc = 1; continue; }
        chmod(target, mode);
        if (verbose) printf("'%s' -> '%s'\n", o.operands[i], target);
    }
    return rc;
}
