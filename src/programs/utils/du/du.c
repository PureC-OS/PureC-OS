#include "../lib/common.h"

static bool o_all, o_human, o_summary, o_bytes, o_total;
static long o_depth = -1;

static void show(unsigned long long bytes, const char *path)
{
    char buf[32];
    if (o_human) util_human(buf, sizeof(buf), bytes);
    else if (o_bytes) snprintf(buf, sizeof(buf), "%llu", bytes);
    else snprintf(buf, sizeof(buf), "%llu", (bytes + 1023) / 1024);
    printf("%s\t%s\n", buf, path);
}

static unsigned long long walk(const char *path, int depth)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        util_err("cannot access '%s': %s", path, strerror(errno));
        return 0;
    }
    unsigned long long size = o_bytes ? (unsigned long long)st.st_size : ((unsigned long long)st.st_size + 4095) / 4096 * 4096;
    if (!S_ISDIR(st.st_mode)) {
        if ((o_all && !o_summary && (o_depth < 0 || depth <= o_depth)) || depth == 0) show(size, path);
        return size;
    }
    DIR *dir = opendir(path);
    if (dir) {
        struct dirent *d;
        char **names = NULL;
        int n = 0;
        while ((d = readdir(dir))) {
            if (!strcmp(d->d_name, ".") || !strcmp(d->d_name, "..")) continue;
            names = realloc(names, sizeof(char *) * (size_t)(n + 1));
            names[n++] = strdup(d->d_name);
        }
        closedir(dir);
        for (int i = 0; i < n; i++) {
            char child[UTIL_PATH];
            util_join(child, sizeof(child), path, names[i]);
            size += walk(child, depth + 1);
            free(names[i]);
        }
        free(names);
    }
    if ((!o_summary && (o_depth < 0 || depth <= o_depth)) || depth == 0) show(size, path);
    return size;
}

int main(int argc, char **argv)
{
    util_init(argc, argv);
    struct opts o;
    opts_begin(&o, argc, argv);
    int c;
    while ((c = opts_next(&o, "ahsbcd:k")) != -1) {
        switch (c) {
        case 'a': o_all = true; break;
        case 'h': o_human = true; break;
        case 's': o_summary = true; break;
        case 'b': o_bytes = true; break;
        case 'c': o_total = true; break;
        case 'd': o_depth = atol(o.arg); break;
        case 'k': break;
        case 'L':
            if (opts_long(&o, "max-depth")) { o_depth = o.value ? atol(o.value) : 0; break; }
            if (opts_long(&o, "help")) { util_usage("[-ahsbck] [-d DEPTH] [FILE]..."); return 0; }
            return util_bad_option(&o, c);
        default: return util_bad_option(&o, c);
        }
    }
    if (!o.count) o.operands[o.count++] = ".";
    unsigned long long total = 0;
    for (int i = 0; i < o.count; i++) total += walk(o.operands[i], 0);
    if (o_total) show(total, "total");
    return 0;
}
