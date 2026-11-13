#include "../lib/fileops.h"

int main(int argc, char **argv)
{
    util_init(argc, argv);
    struct opts o;
    opts_begin(&o, argc, argv);
    bool verbose = false, no_clobber = false, interactive = false;
    const char *target_dir = NULL;
    int c;
    while ((c = opts_next(&o, "vnfit:")) != -1) {
        switch (c) {
        case 'v': verbose = true; break;
        case 'n': no_clobber = true; break;
        case 'f': no_clobber = interactive = false; break;
        case 'i': interactive = true; break;
        case 't': target_dir = o.arg; break;
        case 'L':
            if (opts_long(&o, "help")) { util_usage("[-vnfi] SOURCE... DEST"); return 0; }
            return util_bad_option(&o, c);
        default: return util_bad_option(&o, c);
        }
    }
    int nsrc = o.count;
    const char *dest = target_dir;
    if (!dest) {
        if (o.count < 2) return util_err("missing file operand");
        dest = o.operands[--nsrc];
    }
    bool dest_dir = util_is_dir(dest);
    if (nsrc > 1 && !dest_dir) return util_err("target '%s' is not a directory", dest);
    int rc = 0;
    for (int i = 0; i < nsrc; i++) {
        const char *src = o.operands[i];
        if (!util_exists(src)) {
            rc = util_err("cannot stat '%s': %s", src, strerror(ENOENT));
            continue;
        }
        char target[UTIL_PATH];
        if (dest_dir) util_join(target, sizeof(target), dest, util_base(src));
        else snprintf(target, sizeof(target), "%s", dest);
        if (util_exists(target)) {
            if (no_clobber) continue;
            if (interactive) {
                fprintf(stderr, "%s: overwrite '%s'? ", prog, target);
                char answer[16];
                if (!fgets(answer, sizeof(answer), stdin) || (answer[0] != 'y' && answer[0] != 'Y')) continue;
            }
        }
        if (rename(src, target) != 0) {
            if (fo_copy_tree(src, target, true, false, false) != 0 || fo_remove_tree(src, false, false) != 0) {
                rc = 1;
                continue;
            }
        }
        if (verbose) printf("renamed '%s' -> '%s'\n", src, target);
    }
    return rc;
}
