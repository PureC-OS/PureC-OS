#include "fileops.h"

int main(int argc, char **argv)
{
    util_init(argc, argv);
    struct opts o;
    opts_begin(&o, argc, argv);
    bool recursive = false, force = false, verbose = false, interactive = false, dirs = false;
    int c;
    while ((c = opts_next(&o, "rRfvid")) != -1) {
        switch (c) {
        case 'r': case 'R': recursive = true; break;
        case 'f': force = true; interactive = false; break;
        case 'v': verbose = true; break;
        case 'i': interactive = true; force = false; break;
        case 'd': dirs = true; break;
        case 'L':
            if (opts_long(&o, "recursive")) { recursive = true; break; }
            if (opts_long(&o, "force")) { force = true; break; }
            if (opts_long(&o, "help")) { util_usage("[-rfvid] FILE..."); return 0; }
            return util_bad_option(&o, c);
        default: return util_bad_option(&o, c);
        }
    }
    if (!o.count) {
        if (force) return 0;
        return util_err("missing operand");
    }
    int rc = 0;
    for (int i = 0; i < o.count; i++) {
        const char *path = o.operands[i];
        const char *base = util_base(path);
        if (!strcmp(base, ".") || !strcmp(base, "..")) {
            rc = util_err("refusing to remove '.' or '..' directory: skipping '%s'", path);
            continue;
        }
        if (!strcmp(path, "/")) {
            rc = util_err("it is dangerous to operate recursively on '/'");
            continue;
        }
        struct stat st;
        if (stat(path, &st) != 0) {
            if (!force) rc = util_err("cannot remove '%s': %s", path, strerror(errno));
            continue;
        }
        if (interactive) {
            fprintf(stderr, "%s: remove %s '%s'? ", prog, S_ISDIR(st.st_mode) ? "directory" : "file", path);
            char answer[16];
            if (!fgets(answer, sizeof(answer), stdin) || (answer[0] != 'y' && answer[0] != 'Y')) continue;
        }
        if (S_ISDIR(st.st_mode)) {
            if (recursive) rc |= fo_remove_tree(path, verbose, force);
            else if (dirs) {
                if (rmdir(path) != 0) rc = util_err("cannot remove '%s': %s", path, strerror(errno));
                else if (verbose) printf("removed directory '%s'\n", path);
            } else rc = util_err("cannot remove '%s': Is a directory", path);
            continue;
        }
        if (unlink(path) != 0) rc = util_err("cannot remove '%s': %s", path, strerror(errno));
        else if (verbose) printf("removed '%s'\n", path);
    }
    return rc;
}
