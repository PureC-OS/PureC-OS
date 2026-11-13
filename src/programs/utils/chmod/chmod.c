#include "../lib/common.h"

static bool recursive, verbose, changes, quiet;

static int apply(const char *path, const char *spec)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        if (!quiet) util_err("cannot access '%s': %s", path, strerror(errno));
        return 1;
    }
    unsigned int mode = st.st_mode & 07777;
    unsigned int old = mode;
    if (!util_parse_mode(spec, &mode, S_ISDIR(st.st_mode))) return util_err("invalid mode: '%s'", spec);
    int rc = 0;
    if (chmod(path, mode) != 0) {
        if (!quiet) util_err("changing permissions of '%s': %s", path, strerror(errno));
        rc = 1;
    } else if (verbose || (changes && old != mode)) {
        char a[11], b[11];
        util_mode_string((mode_t)(old | (st.st_mode & S_IFMT)), a);
        util_mode_string((mode_t)(mode | (st.st_mode & S_IFMT)), b);
        if (old == mode) printf("mode of '%s' retained as %04o (%s)\n", path, mode, b + 1);
        else printf("mode of '%s' changed from %04o (%s) to %04o (%s)\n", path, old, a + 1, mode, b + 1);
    }
    if (recursive && S_ISDIR(st.st_mode)) {
        DIR *dir = opendir(path);
        if (!dir) return util_perr(path);
        struct dirent *d;
        while ((d = readdir(dir))) {
            if (!strcmp(d->d_name, ".") || !strcmp(d->d_name, "..")) continue;
            char child[UTIL_PATH];
            util_join(child, sizeof(child), path, d->d_name);
            rc |= apply(child, spec);
        }
        closedir(dir);
    }
    return rc;
}

int main(int argc, char **argv)
{
    util_init(argc, argv);
    int first = 1;
    while (first < argc && argv[first][0] == '-' && argv[first][1] && strchr("Rvcf", argv[first][1]) && !strchr(argv[first], '=')) {
        for (const char *p = argv[first] + 1; *p; p++) {
            if (*p == 'R') recursive = true;
            else if (*p == 'v') verbose = true;
            else if (*p == 'c') changes = true;
            else if (*p == 'f') quiet = true;
            else return util_err("invalid option -- '%c'", *p);
        }
        first++;
    }
    if (first < argc && !strcmp(argv[first], "--help")) {
        util_usage("[-Rvcf] MODE FILE...");
        return 0;
    }
    if (argc - first < 2) return util_err("missing operand");
    const char *spec = argv[first];
    int rc = 0;
    for (int i = first + 1; i < argc; i++) rc |= apply(argv[i], spec);
    return rc;
}
