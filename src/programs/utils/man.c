#include "common.h"
#include "pager.h"
#include <ctype.h>

#define MAN_DIR "/usr/share/man"

static bool color;

static bool find_page(const char *name, const char *section, char *out, size_t cap)
{
    if (strchr(name, '/')) {
        snprintf(out, cap, "%s", name);
        return util_exists(out);
    }
    const char *order = "18576234";
    for (const char *s = order; *s; s++) {
        if (section && section[0] != *s) continue;
        snprintf(out, cap, "%s/%s.%c", MAN_DIR, name, *s);
        if (util_exists(out)) return true;
    }
    return false;
}

static void append(char **buf, size_t *len, size_t *cap, const char *text, size_t n)
{
    if (*len + n + 1 > *cap) {
        while (*len + n + 1 > *cap) *cap = *cap ? *cap * 2 : 256;
        *buf = realloc(*buf, *cap);
    }
    memcpy(*buf + *len, text, n);
    *len += n;
    (*buf)[*len] = 0;
}

static void appends(char **buf, size_t *len, size_t *cap, const char *text)
{
    append(buf, len, cap, text, strlen(text));
}

static char *render_inline(const char *line, bool in_synopsis)
{
    char *out = NULL;
    size_t len = 0, cap = 0;
    appends(&out, &len, &cap, "");
    const char *p = line;
    int indent = 0;
    while (p[indent] == ' ') indent++;
    bool bold = false, under = false;
    if (color && indent > 0 && indent <= 8 && p[indent] == '-') {
        appends(&out, &len, &cap, (const char *)"");
        append(&out, &len, &cap, p, (size_t)indent);
        p += indent;
        const char *end = p;
        while (*end && !(end[0] == ' ' && end[1] == ' ') && !(end[0] == ' ' && end[1] == 0)) end++;
        appends(&out, &len, &cap, C_BOLD C_YELLOW);
        append(&out, &len, &cap, p, (size_t)(end - p));
        appends(&out, &len, &cap, C_RESET);
        p = end;
    } else if (color && in_synopsis && indent > 0) {
        append(&out, &len, &cap, p, (size_t)indent);
        p += indent;
        const char *end = p;
        while (*end && *end != ' ') end++;
        appends(&out, &len, &cap, C_BOLD);
        append(&out, &len, &cap, p, (size_t)(end - p));
        appends(&out, &len, &cap, C_RESET);
        p = end;
    }
    while (*p) {
        if (p[0] == '*' && p[1] == '*') {
            bold = !bold;
            if (color) appends(&out, &len, &cap, bold ? C_BOLD : C_RESET);
            if (color && !bold && under) appends(&out, &len, &cap, C_UNDER);
            p += 2;
            continue;
        }
        if (p[0] == '_' && p[1] == '_') {
            under = !under;
            if (color) appends(&out, &len, &cap, under ? C_UNDER C_CYAN : C_RESET);
            if (color && !under && bold) appends(&out, &len, &cap, C_BOLD);
            p += 2;
            continue;
        }
        append(&out, &len, &cap, p, 1);
        p++;
    }
    if (color) appends(&out, &len, &cap, C_RESET);
    return out;
}

static void upper(char *out, size_t cap, const char *in)
{
    size_t i = 0;
    for (; in[i] && i + 1 < cap; i++) out[i] = (char)toupper((unsigned char)in[i]);
    out[i] = 0;
}

static int show_page(const char *path, const char *name)
{
    FILE *f = fopen(path, "r");
    if (!f) return util_perr(path);
    char sec = '1';
    const char *dot = strrchr(path, '.');
    if (dot && isdigit((unsigned char)dot[1])) sec = dot[1];
    struct pager_text text = {0};
    char title[64], header[512];
    upper(title, sizeof(title), name);
    int width = util_term_width();
    if (width > 100) width = 100;
    char left[80];
    snprintf(left, sizeof(left), "%s(%c)", title, sec);
    const char *mid = "PureC OS Manual";
    int gap = width - (int)strlen(left) * 2 - (int)strlen(mid);
    if (gap < 2) gap = 2;
    snprintf(header, sizeof(header), "%s%s%*s%s%*s%s%s", color ? C_BOLD : "", left, gap / 2, "", mid,
             gap - gap / 2, "", left, color ? C_RESET : "");
    pager_add(&text, header);
    pager_add(&text, "");
    char *line = NULL;
    size_t cap = 0;
    ssize_t len;
    bool synopsis = false;
    while ((len = util_getline(&line, &cap, f)) >= 0) {
        if (len && line[len - 1] == '\n') line[--len] = 0;
        for (char *t = line; *t; t++)
            if (*t == '\t') *t = ' ';
        if (line[0] && line[0] != ' ') {
            synopsis = !strcmp(line, "SYNOPSIS");
            char buf[256];
            snprintf(buf, sizeof(buf), "%s%s%s", color ? C_BOLD C_BLUE : "", line, color ? C_RESET : "");
            pager_add(&text, buf);
            continue;
        }
        char *r = render_inline(line, synopsis);
        pager_add(&text, r);
        free(r);
    }
    free(line);
    fclose(f);
    pager_add(&text, "");
    char footer[256];
    snprintf(footer, sizeof(footer), "%sPureC OS 1.2.29%*s%s(%c)%s", color ? C_DIM : "", width - 15 - (int)strlen(name) - 3, "",
             name, sec, color ? C_RESET : "");
    pager_add(&text, footer);
    char status[96];
    snprintf(status, sizeof(status), "Manual page %s(%c)", name, sec);
    pager_show(&text, status, true);
    pager_free(&text);
    return 0;
}

static bool read_whatis(const char *path, char *out, size_t cap)
{
    FILE *f = fopen(path, "r");
    if (!f) return false;
    char *line = NULL;
    size_t lcap = 0;
    ssize_t len;
    bool in_name = false, found = false;
    while ((len = util_getline(&line, &lcap, f)) >= 0) {
        if (len && line[len - 1] == '\n') line[--len] = 0;
        if (!strcmp(line, "NAME")) { in_name = true; continue; }
        if (in_name) {
            char *s = line;
            while (*s == ' ') s++;
            if (!*s) continue;
            const char *dash = strstr(s, " - ");
            snprintf(out, cap, "%s", dash ? dash + 3 : s);
            found = true;
            break;
        }
    }
    free(line);
    fclose(f);
    return found;
}

static int cmp_str(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static int apropos(const char *keyword, bool exact)
{
    DIR *dir = opendir(MAN_DIR);
    if (!dir) return util_err("cannot open %s: %s", MAN_DIR, strerror(errno));
    char **names = NULL;
    int n = 0;
    struct dirent *d;
    while ((d = readdir(dir))) {
        if (d->d_name[0] == '.') continue;
        names = realloc(names, sizeof(char *) * (size_t)(n + 1));
        names[n++] = strdup(d->d_name);
    }
    closedir(dir);
    qsort(names, (size_t)n, sizeof(char *), cmp_str);
    int hits = 0;
    for (int i = 0; i < n; i++) {
        char name[128];
        snprintf(name, sizeof(name), "%s", names[i]);
        char *dot = strrchr(name, '.');
        char sec = '1';
        if (dot) { sec = dot[1]; *dot = 0; }
        char path[UTIL_PATH], desc[256] = "";
        util_join(path, sizeof(path), MAN_DIR, names[i]);
        read_whatis(path, desc, sizeof(desc));
        bool match;
        if (exact) match = !strcmp(name, keyword);
        else match = !strcmp(keyword, ".") || strcasestr(name, keyword) || strcasestr(desc, keyword);
        if (!match) continue;
        hits++;
        if (color) printf(C_BOLD "%-12s" C_RESET " (%c)  - %s\n", name, sec, desc);
        else printf("%-12s (%c)  - %s\n", name, sec, desc);
        free(names[i]);
    }
    free(names);
    if (!hits) {
        printf("%s: nothing appropriate.\n", keyword);
        return 16;
    }
    return 0;
}

int main(int argc, char **argv)
{
    util_init(argc, argv);
    struct opts o;
    opts_begin(&o, argc, argv);
    bool keyword = !strcmp(prog, "apropos"), whatis = !strcmp(prog, "whatis");
    bool where = false;
    int c;
    while ((c = opts_next(&o, "kfw")) != -1) {
        if (c == 'k') keyword = true;
        else if (c == 'f') whatis = true;
        else if (c == 'w') where = true;
        else if (c == 'L' && opts_long(&o, "help")) {
            util_usage("[-k KEYWORD] [-f NAME] [-w] [SECTION] NAME...");
            return 0;
        } else return util_bad_option(&o, c);
    }
    color = util_color(1, NULL);
    if (keyword || whatis) {
        if (!o.count) return util_err("%s what?", keyword ? "apropos" : "whatis");
        int rc = 0;
        for (int i = 0; i < o.count; i++) rc |= apropos(o.operands[i], whatis);
        return rc;
    }
    if (!o.count) {
        fprintf(stderr, "What manual page do you want?\nFor example, try 'man ls', 'man intro' or 'man -k .'\n");
        return 1;
    }
    const char *section = NULL;
    int first = 0;
    if (o.count > 1 && isdigit((unsigned char)o.operands[0][0]) && !o.operands[0][1]) {
        section = o.operands[0];
        first = 1;
    }
    int rc = 0;
    for (int i = first; i < o.count; i++) {
        char path[UTIL_PATH];
        if (!find_page(o.operands[i], section, path, sizeof(path))) {
            if (section) fprintf(stderr, "No entry for %s in section %s of the manual\n", o.operands[i], section);
            else fprintf(stderr, "No manual entry for %s\n", o.operands[i]);
            rc = 16;
            continue;
        }
        if (where) {
            puts(path);
            continue;
        }
        rc |= show_page(path, util_base(o.operands[i]));
    }
    return rc;
}
