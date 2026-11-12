#include "fileops.h"
#include <utime.h>

int fo_copy_file(const char *from, const char *to, bool preserve)
{
    struct stat st;
    if (stat(from, &st) != 0) return util_perr(from);
    int in = open(from, O_RDONLY);
    if (in < 0) return util_perr(from);
    int out = open(to, O_WRONLY | O_CREAT | O_TRUNC, (int)(st.st_mode & 0777));
    if (out < 0) {
        close(in);
        return util_perr(to);
    }
    static char buf[32768];
    ssize_t n;
    int rc = 0;
    while ((n = read(in, buf, sizeof(buf))) > 0) {
        ssize_t off = 0;
        while (off < n) {
            ssize_t w = write(out, buf + off, (size_t)(n - off));
            if (w <= 0) {
                rc = util_perr(to);
                break;
            }
            off += w;
        }
        if (rc) break;
    }
    if (n < 0) rc = util_perr(from);
    close(in);
    close(out);
    chmod(to, st.st_mode & 07777);
    if (preserve) {
        struct utimbuf times = {st.st_atime, st.st_mtime};
        utime(to, &times);
    }
    return rc;
}

int fo_copy_tree(const char *from, const char *to, bool preserve, bool verbose, bool no_clobber)
{
    struct stat st;
    if (stat(from, &st) != 0) return util_perr(from);
    if (!S_ISDIR(st.st_mode)) {
        if (no_clobber && util_exists(to)) return 0;
        if (verbose) printf("'%s' -> '%s'\n", from, to);
        return fo_copy_file(from, to, preserve);
    }
    if (!util_is_dir(to)) {
        if (mkdir(to, st.st_mode & 0777) != 0) return util_perr(to);
        if (verbose) printf("'%s' -> '%s'\n", from, to);
    }
    DIR *dir = opendir(from);
    if (!dir) return util_perr(from);
    struct dirent *d;
    int rc = 0;
    char **names = NULL;
    int n = 0;
    while ((d = readdir(dir))) {
        if (!strcmp(d->d_name, ".") || !strcmp(d->d_name, "..")) continue;
        names = realloc(names, sizeof(char *) * (size_t)(n + 1));
        names[n++] = strdup(d->d_name);
    }
    closedir(dir);
    for (int i = 0; i < n; i++) {
        char a[UTIL_PATH], b[UTIL_PATH];
        util_join(a, sizeof(a), from, names[i]);
        util_join(b, sizeof(b), to, names[i]);
        rc |= fo_copy_tree(a, b, preserve, verbose, no_clobber);
        free(names[i]);
    }
    free(names);
    return rc;
}

int fo_remove_tree(const char *path, bool verbose, bool force)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        if (force) return 0;
        return util_err("cannot remove '%s': %s", path, strerror(errno));
    }
    if (S_ISDIR(st.st_mode)) {
        DIR *dir = opendir(path);
        if (!dir) return util_perr(path);
        char **names = NULL;
        int n = 0;
        struct dirent *d;
        while ((d = readdir(dir))) {
            if (!strcmp(d->d_name, ".") || !strcmp(d->d_name, "..")) continue;
            names = realloc(names, sizeof(char *) * (size_t)(n + 1));
            names[n++] = strdup(d->d_name);
        }
        closedir(dir);
        int rc = 0;
        for (int i = 0; i < n; i++) {
            char child[UTIL_PATH];
            util_join(child, sizeof(child), path, names[i]);
            rc |= fo_remove_tree(child, verbose, force);
            free(names[i]);
        }
        free(names);
        if (rmdir(path) != 0) return util_err("cannot remove '%s': %s", path, strerror(errno));
        if (verbose) printf("removed directory '%s'\n", path);
        return rc;
    }
    if (unlink(path) != 0) return util_err("cannot remove '%s': %s", path, strerror(errno));
    if (verbose) printf("removed '%s'\n", path);
    return 0;
}

int fo_mkdirs(const char *path, unsigned int mode, bool verbose)
{
    char buf[UTIL_PATH];
    snprintf(buf, sizeof(buf), "%s", path);
    size_t len = strlen(buf);
    while (len > 1 && buf[len - 1] == '/') buf[--len] = 0;
    for (char *p = buf + 1; ; p++) {
        if (*p == '/' || *p == 0) {
            char saved = *p;
            *p = 0;
            if (!util_is_dir(buf)) {
                if (mkdir(buf, saved ? 0777 : mode) != 0 && !util_is_dir(buf))
                    return util_err("cannot create directory '%s': %s", buf, strerror(errno));
                if (verbose) printf("%s: created directory '%s'\n", prog, buf);
            }
            *p = saved;
            if (!saved) break;
        }
    }
    return 0;
}
