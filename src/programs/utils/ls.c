#include "common.h"
#include <time.h>

struct entry {
    char *name;
    char *path;
    struct stat st;
    bool ok;
};

static bool opt_all, opt_almost, opt_long, opt_human, opt_one, opt_recursive, opt_dirself;
static bool opt_reverse, opt_classify, opt_inode, opt_numeric, opt_nogroup, opt_dirs_first, opt_comma;
static bool opt_size_blocks;
static char sort_key = 'n';
static bool color;
static int status;
static time_t now;

static int cmp_entries(const void *a, const void *b)
{
    const struct entry *x = a, *y = b;
    if (opt_dirs_first) {
        bool dx = S_ISDIR(x->st.st_mode), dy = S_ISDIR(y->st.st_mode);
        if (dx != dy) return dx ? -1 : 1;
    }
    int r = 0;
    if (sort_key == 't') {
        if (x->st.st_mtime != y->st.st_mtime) r = x->st.st_mtime > y->st.st_mtime ? -1 : 1;
    } else if (sort_key == 'S') {
        if (x->st.st_size != y->st.st_size) r = x->st.st_size > y->st.st_size ? -1 : 1;
    } else if (sort_key == 'X') {
        const char *ex = strrchr(x->name, '.'), *ey = strrchr(y->name, '.');
        r = strcasecmp(ex ? ex : "", ey ? ey : "");
    }
    if (!r && sort_key != 'U') {
        const char *nx = x->name, *ny = y->name;
        while (*nx == '.') nx++;
        while (*ny == '.') ny++;
        r = strcasecmp(nx, ny);
        if (!r) r = strcmp(x->name, y->name);
    }
    return opt_reverse ? -r : r;
}

static char indicator(mode_t mode)
{
    if (S_ISDIR(mode)) return '/';
    if (S_ISLNK(mode)) return '@';
    if (S_ISFIFO(mode)) return '|';
    if (S_ISSOCK(mode)) return '=';
    if (mode & 0111) return '*';
    return 0;
}

static int name_width(const struct entry *e)
{
    int w = 0;
    for (const unsigned char *c = (const unsigned char *)e->name; *c; c++)
        if ((*c & 0xC0) != 0x80) w++;
    if (opt_classify && indicator(e->st.st_mode)) w++;
    return w;
}

static void print_name(const struct entry *e)
{
    const char *col = color ? util_name_color(e->name, e->st.st_mode) : NULL;
    if (col) fputs(col, stdout);
    fputs(e->name, stdout);
    if (col) fputs(C_RESET, stdout);
    if (opt_classify) {
        char ind = indicator(e->st.st_mode);
        if (ind) putchar(ind);
    }
}

static void format_size(char *out, size_t cap, const struct entry *e)
{
    if (opt_human) util_human(out, cap, (unsigned long long)e->st.st_size);
    else snprintf(out, cap, "%llu", (unsigned long long)e->st.st_size);
}

static void format_time(char *out, size_t cap, time_t t)
{
    struct tm *tm = localtime(&t);
    if (!tm) {
        snprintf(out, cap, "?");
        return;
    }
    long age = (long)(now - t);
    if (age > 15552000L || age < -3600L) strftime(out, cap, "%b %e  %Y", tm);
    else strftime(out, cap, "%b %e %H:%M", tm);
}

static void print_long(struct entry *list, int n)
{
    int wsize = 1, wuser = 1, wgroup = 1, winode = 1;
    char buf[64], ubuf[64], gbuf[64];
    unsigned long long total = 0;
    for (int i = 0; i < n; i++) {
        format_size(buf, sizeof(buf), &list[i]);
        if ((int)strlen(buf) > wsize) wsize = (int)strlen(buf);
        if (opt_numeric) snprintf(ubuf, sizeof(ubuf), "%u", (unsigned)list[i].st.st_uid);
        else util_user(list[i].st.st_uid, ubuf, sizeof(ubuf));
        if ((int)strlen(ubuf) > wuser) wuser = (int)strlen(ubuf);
        if (opt_numeric) snprintf(gbuf, sizeof(gbuf), "%u", (unsigned)list[i].st.st_gid);
        else util_group(list[i].st.st_gid, gbuf, sizeof(gbuf));
        if ((int)strlen(gbuf) > wgroup) wgroup = (int)strlen(gbuf);
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)list[i].st.st_ino);
        if ((int)strlen(buf) > winode) winode = (int)strlen(buf);
        total += ((unsigned long long)list[i].st.st_size + 1023) / 1024;
    }
    for (int i = 0; i < n; i++) {
        struct entry *e = &list[i];
        char mode[11], tbuf[32];
        util_mode_string(e->st.st_mode, mode);
        if (opt_inode) printf("%*lu ", winode, (unsigned long)e->st.st_ino);
        if (color) {
            for (int k = 0; k < 10; k++) {
                char c = mode[k];
                const char *col = c == 'd' ? C_BOLD C_BLUE : c == 'r' ? C_YELLOW : c == 'w' ? C_RED
                                : c == 'x' || c == 's' || c == 't' ? C_GREEN : c == '-' ? C_GRAY : C_CYAN;
                printf("%s%c" C_RESET, col, c);
            }
        } else {
            fputs(mode, stdout);
        }
        printf(" %2lu ", (unsigned long)(e->st.st_nlink ? e->st.st_nlink : 1));
        if (opt_numeric) snprintf(ubuf, sizeof(ubuf), "%u", (unsigned)e->st.st_uid);
        else util_user(e->st.st_uid, ubuf, sizeof(ubuf));
        printf("%s%-*s%s ", color ? C_BOLD C_YELLOW : "", wuser, ubuf, color ? C_RESET : "");
        if (!opt_nogroup) {
            if (opt_numeric) snprintf(gbuf, sizeof(gbuf), "%u", (unsigned)e->st.st_gid);
            else util_group(e->st.st_gid, gbuf, sizeof(gbuf));
            printf("%s%-*s%s ", color ? C_YELLOW : "", wgroup, gbuf, color ? C_RESET : "");
        }
        format_size(buf, sizeof(buf), e);
        printf("%s%*s%s ", color ? C_BOLD C_GREEN : "", wsize, buf, color ? C_RESET : "");
        format_time(tbuf, sizeof(tbuf), e->st.st_mtime);
        printf("%s%s%s ", color ? C_BLUE : "", tbuf, color ? C_RESET : "");
        print_name(e);
        putchar('\n');
    }
    (void)total;
}

static void print_columns(struct entry *list, int n)
{
    if (!n) return;
    int width = util_term_width();
    int *w = malloc(sizeof(int) * (size_t)n);
    int maxw = 1;
    for (int i = 0; i < n; i++) {
        w[i] = name_width(&list[i]) + (opt_inode ? 8 : 0);
        if (w[i] > maxw) maxw = w[i];
    }
    int rows = n, cols = 1;
    int *colw = malloc(sizeof(int) * (size_t)n);
    for (int c = n; c >= 1; c--) {
        int r = (n + c - 1) / c;
        int total = 0;
        bool fits = true;
        for (int col = 0; col < c && fits; col++) {
            int cw = 0;
            for (int row = 0; row < r; row++) {
                int idx = col * r + row;
                if (idx < n && w[idx] > cw) cw = w[idx];
            }
            colw[col] = cw;
            total += cw + (col + 1 < c ? 2 : 0);
            if (total > width) fits = false;
        }
        if (fits) {
            rows = r;
            cols = (n + r - 1) / r;
            break;
        }
    }
    for (int col = 0; col < cols; col++) {
        int cw = 0;
        for (int row = 0; row < rows; row++) {
            int idx = col * rows + row;
            if (idx < n && w[idx] > cw) cw = w[idx];
        }
        colw[col] = cw;
    }
    for (int row = 0; row < rows; row++) {
        for (int col = 0; col < cols; col++) {
            int idx = col * rows + row;
            if (idx >= n) continue;
            if (opt_inode) printf("%7lu ", (unsigned long)list[idx].st.st_ino);
            print_name(&list[idx]);
            int next = (col + 1) * rows + row;
            if (col + 1 < cols && next < n)
                for (int k = w[idx]; k < colw[col] + 2; k++) putchar(' ');
        }
        putchar('\n');
    }
    free(colw);
    free(w);
}

static void print_list(struct entry *list, int n)
{
    if (opt_long) print_long(list, n);
    else if (opt_comma) {
        for (int i = 0; i < n; i++) {
            print_name(&list[i]);
            fputs(i + 1 < n ? ", " : "\n", stdout);
        }
    } else if (opt_one || !isatty(1)) {
        for (int i = 0; i < n; i++) {
            if (opt_inode) printf("%lu ", (unsigned long)list[i].st.st_ino);
            print_name(&list[i]);
            putchar('\n');
        }
    } else print_columns(list, n);
}

static void free_list(struct entry *list, int n)
{
    for (int i = 0; i < n; i++) {
        free(list[i].name);
        free(list[i].path);
    }
    free(list);
}

static void list_directory(const char *path, bool header, bool *first)
{
    DIR *dir = opendir(path);
    if (!dir) {
        util_err("cannot open directory '%s': %s", path, strerror(errno));
        status = 2;
        return;
    }
    int cap = 64, n = 0;
    struct entry *list = malloc(sizeof(struct entry) * (size_t)cap);
    struct dirent *d;
    while ((d = readdir(dir))) {
        const char *name = d->d_name;
        bool dot = name[0] == '.';
        bool special = !strcmp(name, ".") || !strcmp(name, "..");
        if (dot && !opt_all && !opt_almost) continue;
        if (special && !opt_all) continue;
        if (n >= cap) {
            cap *= 2;
            list = realloc(list, sizeof(struct entry) * (size_t)cap);
        }
        struct entry *e = &list[n++];
        e->name = strdup(name);
        char full[UTIL_PATH];
        util_join(full, sizeof(full), path, name);
        e->path = strdup(full);
        e->ok = stat(full, &e->st) == 0;
        if (!e->ok) {
            memset(&e->st, 0, sizeof(e->st));
            e->st.st_mode = d->d_type == DT_DIR ? S_IFDIR : S_IFREG;
        }
    }
    closedir(dir);
    if (sort_key != 'U') qsort(list, (size_t)n, sizeof(struct entry), cmp_entries);
    if (header) {
        if (!*first) putchar('\n');
        if (color) printf(C_BOLD "%s:" C_RESET "\n", path);
        else printf("%s:\n", path);
    }
    *first = false;
    if (opt_long) {
        unsigned long long total = 0;
        for (int i = 0; i < n; i++) total += ((unsigned long long)list[i].st.st_size + 1023) / 1024;
        if (opt_human) {
            char hb[32];
            util_human(hb, sizeof(hb), total * 1024);
            printf("total %s\n", hb);
        } else {
            printf("total %llu\n", total);
        }
    }
    print_list(list, n);
    if (opt_recursive) {
        for (int i = 0; i < n; i++) {
            if (!S_ISDIR(list[i].st.st_mode)) continue;
            if (!strcmp(list[i].name, ".") || !strcmp(list[i].name, "..")) continue;
            list_directory(list[i].path, true, first);
        }
    }
    free_list(list, n);
}

int main(int argc, char **argv)
{
    util_init(argc, argv);
    struct opts o;
    opts_begin(&o, argc, argv);
    const char *color_when = NULL;
    int c;
    while ((c = opts_next(&o, "aAlhh1RdrtSXUFinogpm")) != -1) {
        switch (c) {
        case 'a': opt_all = true; break;
        case 'A': opt_almost = true; break;
        case 'l': opt_long = true; break;
        case 'h': opt_human = true; break;
        case '1': opt_one = true; break;
        case 'R': opt_recursive = true; break;
        case 'd': opt_dirself = true; break;
        case 'r': opt_reverse = true; break;
        case 't': sort_key = 't'; break;
        case 'S': sort_key = 'S'; break;
        case 'X': sort_key = 'X'; break;
        case 'U': sort_key = 'U'; break;
        case 'F': opt_classify = true; break;
        case 'p': opt_classify = true; break;
        case 'i': opt_inode = true; break;
        case 'n': opt_numeric = true; opt_long = true; break;
        case 'o': opt_nogroup = true; opt_long = true; break;
        case 'g': opt_long = true; break;
        case 'm': opt_comma = true; break;
        case 'L':
            if (opts_long(&o, "color") || opts_long(&o, "colour")) color_when = o.value ? o.value : "always";
            else if (opts_long(&o, "all")) opt_all = true;
            else if (opts_long(&o, "almost-all")) opt_almost = true;
            else if (opts_long(&o, "human-readable")) opt_human = true;
            else if (opts_long(&o, "recursive")) opt_recursive = true;
            else if (opts_long(&o, "reverse")) opt_reverse = true;
            else if (opts_long(&o, "classify")) opt_classify = true;
            else if (opts_long(&o, "group-directories-first")) opt_dirs_first = true;
            else if (opts_long(&o, "directory")) opt_dirself = true;
            else if (opts_long(&o, "inode")) opt_inode = true;
            else if (opts_long(&o, "help")) {
                util_usage("[-aAlh1RdrtSXUFinogm] [--color=WHEN] [--group-directories-first] [FILE...]");
                return 0;
            } else return util_bad_option(&o, c);
            break;
        default: return util_bad_option(&o, c);
        }
    }
    (void)opt_size_blocks;
    color = util_color(1, color_when);
    now = time(NULL);
    if (!o.count) o.operands[o.count++] = ".";
    int nfiles = 0, ndirs = 0;
    struct entry *files = calloc((size_t)o.count, sizeof(struct entry));
    struct entry *dirs = calloc((size_t)o.count, sizeof(struct entry));
    for (int i = 0; i < o.count; i++) {
        struct stat st;
        if (stat(o.operands[i], &st) != 0) {
            util_err("cannot access '%s': %s", o.operands[i], strerror(errno));
            status = 2;
            continue;
        }
        struct entry *e = (S_ISDIR(st.st_mode) && !opt_dirself) ? &dirs[ndirs++] : &files[nfiles++];
        e->name = strdup(o.operands[i]);
        e->path = strdup(o.operands[i]);
        e->st = st;
        e->ok = true;
    }
    if (sort_key != 'U') {
        qsort(files, (size_t)nfiles, sizeof(struct entry), cmp_entries);
        qsort(dirs, (size_t)ndirs, sizeof(struct entry), cmp_entries);
    }
    print_list(files, nfiles);
    bool first = nfiles == 0;
    bool header = o.count > 1 || opt_recursive;
    for (int i = 0; i < ndirs; i++) list_directory(dirs[i].path, header, &first);
    free_list(files, nfiles);
    free_list(dirs, ndirs);
    fflush(stdout);
    return status;
}
