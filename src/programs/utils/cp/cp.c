#include "../lib/fileops.h"

int main(int argc, char **argv)
{
    util_init(argc, argv);
    struct opts o;
    opts_begin(&o, argc, argv);
    bool recursive = false, preserve = false, verbose = false, no_clobber = false, force = false, interactive = false;
    const char *target_dir = NULL;
    int c;
    while ((c = opts_next(&o, "rRpvnfiat:")) != -1) {
        switch (c) {
        case 'r': case 'R': recursive = true; break;
        case 'p': preserve = true; break;
        case 'a': recursive = preserve = true; break;
        case 'v': verbose = true; break;
        case 'n': no_clobber = true; break;
        case 'f': force = true; break;
        case 'i': interactive = true; break;
        case 't': target_dir = o.arg; break;
        case 'L':
            if (opts_long(&o, "recursive")) { recursive = true; break; }
            if (opts_long(&o, "help")) { util_usage("[-rpavnfi] SOURCE... DEST"); return 0; }
            return util_bad_option(&o, c);
        default: return util_bad_option(&o, c);
        }
    }
    (void)force;
    int nsrc = o.count;
    const char *dest = target_dir;
    if (!dest) {
        if (o.count < 2) return util_err(o.count ? "missing destination file operand after '%s'" : "missing file operand", o.count ? o.operands[0] : "");
        dest = o.operands[--nsrc];
    }
    bool dest_dir = util_is_dir(dest);
    if (nsrc > 1 && !dest_dir) return util_err("target '%s' is not a directory", dest);
    int rc = 0;
    for (int i = 0; i < nsrc; i++) {
        const char *src = o.operands[i];
        char target[UTIL_PATH];
        if (dest_dir) util_join(target, sizeof(target), dest, util_base(src));
        else snprintf(target, sizeof(target), "%s", dest);
        if (util_is_dir(src) && !recursive) {
            util_err("-r not specified; omitting directory '%s'", src);
            rc = 1;
            continue;
        }
        if (util_exists(target) && interactive) {
            fprintf(stderr, "%s: overwrite '%s'? ", prog, target);
            char answer[16];
            if (!fgets(answer, sizeof(answer), stdin) || (answer[0] != 'y' && answer[0] != 'Y')) continue;
        }
        rc |= fo_copy_tree(src, target, preserve, verbose, no_clobber);
    }
    return rc;
}
