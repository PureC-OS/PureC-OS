#include "../lib/common.h"

static bool o_all, o_dirs, o_size, o_human, o_full;
static long o_level = -1;
static bool color;
static long ndirs, nfiles;

struct item { char *name; struct stat st; };

static int cmp(const void *a, const void *b)
{
    return strcasecmp(((const struct item *)a)->name, ((const struct item *)b)->name);
}

static void walk(const char *path, const char *prefix, int level)
{
    DIR *dir = opendir(path);
    if (!dir) return;
    struct item *items = NULL;
    int n = 0;
    struct dirent *d;
    while ((d = readdir(dir))) {
        if (!strcmp(d->d_name, ".") || !strcmp(d->d_name, "..")) continue;
        if (!o_all && d->d_name[0] == '.') continue;
        char full[UTIL_PATH];
        util_join(full, sizeof(full), path, d->d_name);
        struct stat st;
        if (stat(full, &st) != 0) memset(&st, 0, sizeof(st));
        if (o_dirs && !S_ISDIR(st.st_mode)) continue;
        items = realloc(items, sizeof(struct item) * (size_t)(n + 1));
        items[n].name = strdup(d->d_name);
        items[n].st = st;
        n++;
    }
    closedir(dir);
    qsort(items, (size_t)n, sizeof(struct item), cmp);
    for (int i = 0; i < n; i++) {
        bool last = i == n - 1;
        char full[UTIL_PATH];
        util_join(full, sizeof(full), path, items[i].name);
        printf("%s%s%s%s", color ? C_GRAY : "", prefix, last ? "\xe2\x94\x94\xe2\x94\x80\xe2\x94\x80 " : "\xe2\x94\x9c\xe2\x94\x80\xe2\x94\x80 ", color ? C_RESET : "");
        if (o_size) {
            char buf[32];
            if (o_human) util_human(buf, sizeof(buf), (unsigned long long)items[i].st.st_size);
            else snprintf(buf, sizeof(buf), "%lld", (long long)items[i].st.st_size);
            printf("[%6s]  ", buf);
        }
        const char *col = color ? util_name_color(items[i].name, items[i].st.st_mode) : NULL;
        printf("%s%s%s\n", col ? col : "", o_full ? full : items[i].name, col ? C_RESET : "");
        if (S_ISDIR(items[i].st.st_mode)) {
            ndirs++;
            if (o_level < 0 || level + 1 < o_level) {
                char next[512];
                snprintf(next, sizeof(next), "%s%s", prefix, last ? "    " : "\xe2\x94\x82   ");
                walk(full, next, level + 1);
            }
        } else nfiles++;
        free(items[i].name);
    }
    free(items);
}

int main(int argc, char **argv)
{
    util_init(argc, argv);
    struct opts o;
    opts_begin(&o, argc, argv);
    const char *color_when = NULL;
    int c;
    while ((c = opts_next(&o, "adshfL:Cn")) != -1) {
        switch (c) {
        case 'a': o_all = true; break;
        case 'd': o_dirs = true; break;
        case 's': o_size = true; break;
        case 'h': o_size = o_human = true; break;
        case 'f': o_full = true; break;
        case 'C': color_when = "always"; break;
        case 'n': color_when = "never"; break;
        case 'L':
            if (!o.name) { o_level = atol(o.arg); break; }
            if (opts_long(&o, "help")) { util_usage("[-adshfCn] [-L LEVEL] [DIR]..."); return 0; }
            return util_bad_option(&o, c);
        default: return util_bad_option(&o, c);
        }
    }
    color = util_color(1, color_when);
    if (!o.count) o.operands[o.count++] = ".";
    for (int i = 0; i < o.count; i++) {
        printf("%s%s%s\n", color ? C_BOLD C_BLUE : "", o.operands[i], color ? C_RESET : "");
        walk(o.operands[i], "", 0);
    }
    printf("\n%ld director%s", ndirs, ndirs == 1 ? "y" : "ies");
    if (!o_dirs) printf(", %ld file%s", nfiles, nfiles == 1 ? "" : "s");
    putchar('\n');
    return 0;
}
