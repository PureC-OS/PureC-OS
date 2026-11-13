#include "../lib/common.h"
#include <fnmatch.h>
#include <time.h>

enum { T_NAME, T_INAME, T_PATH, T_TYPE, T_SIZE, T_EMPTY, T_NEWER, T_MTIME, T_PRINT, T_PRINT0, T_EXEC, T_DELETE, T_TRUE, T_PERM };

struct test {
    int kind;
    bool negate;
    bool or_next;
    const char *arg;
    char cmp;
    long long value;
    char **exec;
    int nexec;
};

static struct test tests[64];
static int ntests;
static long maxdepth = -1, mindepth = 0;
static bool has_action;
static int status;
static time_t now;

static bool eval(const struct test *t, const char *path, const struct stat *st)
{
    bool r = true;
    switch (t->kind) {
    case T_NAME: r = fnmatch(t->arg, util_base(path), 0) == 0; break;
    case T_INAME: r = fnmatch(t->arg, util_base(path), FNM_CASEFOLD) == 0; break;
    case T_PATH: r = fnmatch(t->arg, path, 0) == 0; break;
    case T_TYPE: r = (t->arg[0] == 'd' && S_ISDIR(st->st_mode)) || (t->arg[0] == 'f' && S_ISREG(st->st_mode)) ||
                     (t->arg[0] == 'l' && S_ISLNK(st->st_mode)); break;
    case T_SIZE: {
        long long v = (long long)st->st_size;
        r = t->cmp == '+' ? v > t->value : t->cmp == '-' ? v < t->value : v == t->value;
        break;
    }
    case T_MTIME: {
        long long days = (long long)(now - st->st_mtime) / 86400;
        r = t->cmp == '+' ? days > t->value : t->cmp == '-' ? days < t->value : days == t->value;
        break;
    }
    case T_PERM: r = (long long)(st->st_mode & 07777) == t->value; break;
    case T_EMPTY:
        if (S_ISDIR(st->st_mode)) {
            DIR *d = opendir(path);
            r = true;
            struct dirent *e;
            while (d && (e = readdir(d)))
                if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) { r = false; break; }
            if (d) closedir(d);
        } else r = st->st_size == 0;
        break;
    case T_NEWER: {
        struct stat ref;
        r = stat(t->arg, &ref) == 0 && st->st_mtime > ref.st_mtime;
        break;
    }
    case T_PRINT: puts(path); break;
    case T_PRINT0: fputs(path, stdout); putchar(0); break;
    case T_DELETE:
        if (S_ISDIR(st->st_mode) ? rmdir(path) : unlink(path)) {
            util_err("cannot delete '%s': %s", path, strerror(errno));
            status = 1;
            r = false;
        }
        break;
    case T_EXEC: {
        char **args = calloc((size_t)t->nexec + 1, sizeof(char *));
        for (int i = 0; i < t->nexec; i++) args[i] = !strcmp(t->exec[i], "{}") ? (char *)path : t->exec[i];
        r = util_exec_wait(args) == 0;
        free(args);
        break;
    }
    case T_TRUE: break;
    }
    return t->negate ? !r : r;
}

static bool matches(const char *path, const struct stat *st)
{
    bool result = true, group = true;
    for (int i = 0; i < ntests; i++) {
        if (group) group = eval(&tests[i], path, st);
        if (tests[i].or_next || i == ntests - 1) {
            if (group) return true;
            result = false;
            group = true;
        }
    }
    return result;
}

static void walk(const char *path, int depth)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        util_err("'%s': %s", path, strerror(errno));
        status = 1;
        return;
    }
    bool is_dir = S_ISDIR(st.st_mode);
    bool delete_mode = false;
    for (int i = 0; i < ntests; i++) if (tests[i].kind == T_DELETE) delete_mode = true;
    if (!delete_mode && depth >= mindepth && matches(path, &st) && !has_action) puts(path);
    if (is_dir && (maxdepth < 0 || depth < maxdepth)) {
        DIR *dir = opendir(path);
        if (!dir) {
            util_err("'%s': %s", path, strerror(errno));
            status = 1;
        } else {
            char **names = NULL;
            int n = 0;
            struct dirent *d;
            while ((d = readdir(dir))) {
                if (!strcmp(d->d_name, ".") || !strcmp(d->d_name, "..")) continue;
                names = realloc(names, sizeof(char *) * (size_t)(n + 1));
                names[n++] = strdup(d->d_name);
            }
            closedir(dir);
            for (int i = 0; i < n; i++) {
                char child[UTIL_PATH];
                util_join(child, sizeof(child), path, names[i]);
                walk(child, depth + 1);
                free(names[i]);
            }
            free(names);
        }
    }
    if (delete_mode && depth >= mindepth && depth > 0) matches(path, &st);
}

static bool parse_num(const char *s, char *cmp, long long *v, bool size)
{
    *cmp = 0;
    if (*s == '+' || *s == '-') *cmp = *s++;
    char *end;
    long long n = strtoll(s, &end, 10);
    if (end == s) return false;
    if (size) {
        switch (*end) {
        case 'c': break;
        case 'k': n *= 1024; break;
        case 'M': n *= 1024 * 1024; break;
        case 'G': n *= 1024LL * 1024 * 1024; break;
        case 0: n *= 512; break;
        default: return false;
        }
    } else if (*end) return false;
    *v = n;
    return true;
}

int main(int argc, char **argv)
{
    util_init(argc, argv);
    now = time(NULL);
    int i = 1;
    char *paths[64];
    int npaths = 0;
    while (i < argc && argv[i][0] != '-' && argv[i][0] != '!' && argv[i][0] != '(' && npaths < 64) paths[npaths++] = argv[i++];
    if (!npaths) paths[npaths++] = ".";
    bool negate = false;
    for (; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "!") || !strcmp(a, "-not")) { negate = !negate; continue; }
        if (!strcmp(a, "-o") || !strcmp(a, "-or")) { if (ntests) tests[ntests - 1].or_next = true; continue; }
        if (!strcmp(a, "-a") || !strcmp(a, "-and")) continue;
        if (!strcmp(a, "--help")) { util_usage("[PATH]... [-name PAT] [-iname PAT] [-type f|d] [-size [+-]N[ckMG]] [-maxdepth N] [-exec CMD {} ;] [-delete]"); return 0; }
        if (ntests >= 63) return util_err("too many expressions");
        struct test *t = &tests[ntests];
        memset(t, 0, sizeof(*t));
        t->negate = negate;
        negate = false;
        bool need = strcmp(a, "-print") && strcmp(a, "-print0") && strcmp(a, "-empty") && strcmp(a, "-delete") &&
                    strcmp(a, "-true") && strcmp(a, "-exec") && strcmp(a, "-false");
        if (need && i + 1 >= argc) return util_err("missing argument to '%s'", a);
        if (!strcmp(a, "-name")) { t->kind = T_NAME; t->arg = argv[++i]; }
        else if (!strcmp(a, "-iname")) { t->kind = T_INAME; t->arg = argv[++i]; }
        else if (!strcmp(a, "-path") || !strcmp(a, "-wholename")) { t->kind = T_PATH; t->arg = argv[++i]; }
        else if (!strcmp(a, "-type")) { t->kind = T_TYPE; t->arg = argv[++i]; }
        else if (!strcmp(a, "-newer")) { t->kind = T_NEWER; t->arg = argv[++i]; }
        else if (!strcmp(a, "-size")) { t->kind = T_SIZE; if (!parse_num(argv[++i], &t->cmp, &t->value, true)) return util_err("invalid -size '%s'", argv[i]); }
        else if (!strcmp(a, "-mtime")) { t->kind = T_MTIME; if (!parse_num(argv[++i], &t->cmp, &t->value, false)) return util_err("invalid -mtime '%s'", argv[i]); }
        else if (!strcmp(a, "-perm")) { t->kind = T_PERM; t->value = strtol(argv[++i], NULL, 8); }
        else if (!strcmp(a, "-maxdepth")) { maxdepth = atol(argv[++i]); continue; }
        else if (!strcmp(a, "-mindepth")) { mindepth = atol(argv[++i]); continue; }
        else if (!strcmp(a, "-empty")) t->kind = T_EMPTY;
        else if (!strcmp(a, "-true")) t->kind = T_TRUE;
        else if (!strcmp(a, "-false")) { t->kind = T_TRUE; t->negate = !t->negate; }
        else if (!strcmp(a, "-print")) { t->kind = T_PRINT; has_action = true; }
        else if (!strcmp(a, "-print0")) { t->kind = T_PRINT0; has_action = true; }
        else if (!strcmp(a, "-delete")) { t->kind = T_DELETE; has_action = true; }
        else if (!strcmp(a, "-exec")) {
            t->kind = T_EXEC;
            has_action = true;
            t->exec = argv + i + 1;
            int k = i + 1;
            while (k < argc && strcmp(argv[k], ";") && strcmp(argv[k], "+")) k++;
            if (k >= argc) return util_err("missing argument to '-exec'");
            t->nexec = k - i - 1;
            i = k;
        } else return util_err("unknown predicate '%s'", a);
        ntests++;
    }
    for (int p = 0; p < npaths; p++) walk(paths[p], 0);
    return status;
}
