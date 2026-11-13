#include "../lib/common.h"
#include <ctype.h>

static bool o_reverse, o_numeric, o_unique, o_fold, o_blanks, o_human, o_version;
static int key_field;
static int key_end;
static char separator;

static const char *field_start(const char *s, int field, const char **end)
{
    const char *p = s;
    for (int f = 1; f < field && *p; f++) {
        if (separator) {
            const char *n = strchr(p, separator);
            if (!n) { p += strlen(p); break; }
            p = n + 1;
        } else {
            while (*p && isspace((unsigned char)*p)) p++;
            while (*p && !isspace((unsigned char)*p)) p++;
        }
    }
    const char *e = p;
    int last = key_end ? key_end : (key_field ? 0 : 0);
    if (last && last >= field) {
        e = p;
        for (int f = field; f <= last && *e; f++) {
            if (separator) {
                const char *n = strchr(e + (f == field ? 0 : 1), separator);
                e = n ? n : e + strlen(e);
            } else {
                while (*e && isspace((unsigned char)*e)) e++;
                while (*e && !isspace((unsigned char)*e)) e++;
            }
        }
    } else e = p + strlen(p);
    *end = e;
    return p;
}

static double human_value(const char *s)
{
    char *end;
    double v = strtod(s, &end);
    switch (toupper((unsigned char)*end)) {
    case 'K': v *= 1e3; break;
    case 'M': v *= 1e6; break;
    case 'G': v *= 1e9; break;
    case 'T': v *= 1e12; break;
    }
    return v;
}

static int version_cmp(const char *a, const char *ae, const char *b, const char *be)
{
    while (a < ae && b < be) {
        if (isdigit((unsigned char)*a) && isdigit((unsigned char)*b)) {
            unsigned long long x = 0, y = 0;
            while (a < ae && isdigit((unsigned char)*a)) x = x * 10 + (unsigned)(*a++ - '0');
            while (b < be && isdigit((unsigned char)*b)) y = y * 10 + (unsigned)(*b++ - '0');
            if (x != y) return x < y ? -1 : 1;
        } else {
            if (*a != *b) return (unsigned char)*a < (unsigned char)*b ? -1 : 1;
            a++;
            b++;
        }
    }
    return (a < ae) - (b < be);
}

static int compare(const void *pa, const void *pb)
{
    const char *a = *(char *const *)pa, *b = *(char *const *)pb;
    const char *ae, *be;
    if (key_field) {
        a = field_start(a, key_field, &ae);
        b = field_start(b, key_field, &be);
    } else {
        ae = a + strlen(a);
        be = b + strlen(b);
    }
    if (o_blanks || o_numeric || o_human) {
        while (a < ae && isspace((unsigned char)*a)) a++;
        while (b < be && isspace((unsigned char)*b)) b++;
    }
    int r;
    if (o_numeric || o_human) {
        double x = o_human ? human_value(a) : strtod(a, NULL);
        double y = o_human ? human_value(b) : strtod(b, NULL);
        r = x < y ? -1 : x > y ? 1 : 0;
    } else if (o_version) {
        r = version_cmp(a, ae, b, be);
    } else {
        size_t la = (size_t)(ae - a), lb = (size_t)(be - b);
        size_t n = la < lb ? la : lb;
        r = o_fold ? strncasecmp(a, b, n) : memcmp(a, b, n);
        if (!r) r = la < lb ? -1 : la > lb ? 1 : 0;
    }
    return o_reverse ? -r : r;
}

int main(int argc, char **argv)
{
    util_init(argc, argv);
    struct opts o;
    opts_begin(&o, argc, argv);
    const char *output = NULL;
    bool check = false;
    int c;
    while ((c = opts_next(&o, "rnufbhVk:t:o:cs")) != -1) {
        switch (c) {
        case 'r': o_reverse = true; break;
        case 'n': o_numeric = true; break;
        case 'u': o_unique = true; break;
        case 'f': o_fold = true; break;
        case 'b': o_blanks = true; break;
        case 'h': o_human = true; break;
        case 'V': o_version = true; break;
        case 'c': check = true; break;
        case 's': break;
        case 'k': {
            key_field = atoi(o.arg);
            const char *comma = strchr(o.arg, ',');
            key_end = comma ? atoi(comma + 1) : 0;
            for (const char *p = o.arg; *p; p++) {
                if (*p == 'n') o_numeric = true;
                else if (*p == 'r') o_reverse = true;
                else if (*p == 'h') o_human = true;
                else if (*p == 'f') o_fold = true;
            }
            if (key_field < 1) return util_err("invalid key '%s'", o.arg);
            break;
        }
        case 't':
            separator = !strcmp(o.arg, "\\t") ? '\t' : o.arg[0];
            break;
        case 'o': output = o.arg; break;
        case 'L':
            if (opts_long(&o, "help")) { util_usage("[-rnufbhVc] [-k N[,M]] [-t SEP] [-o FILE] [FILE]..."); return 0; }
            return util_bad_option(&o, c);
        default: return util_bad_option(&o, c);
        }
    }
    if (!o.count) o.operands[o.count++] = "-";
    char **lines = NULL;
    size_t n = 0, cap = 0;
    int rc = 0;
    for (int i = 0; i < o.count; i++) {
        FILE *f = util_fopen(o.operands[i]);
        if (!f) { rc = util_perr(o.operands[i]); continue; }
        char *line = NULL;
        size_t lcap = 0;
        ssize_t len;
        while ((len = util_getline(&line, &lcap, f)) >= 0) {
            if (len && line[len - 1] == '\n') line[len - 1] = 0;
            if (n == cap) {
                cap = cap ? cap * 2 : 1024;
                lines = realloc(lines, cap * sizeof(char *));
            }
            lines[n++] = strdup(line);
        }
        free(line);
        util_fclose(f);
    }
    if (check) {
        for (size_t i = 1; i < n; i++) {
            int r = compare(&lines[i - 1], &lines[i]);
            if (r > 0 || (o_unique && r == 0)) {
                fprintf(stderr, "%s: -:%zu: disorder: %s\n", prog, i + 1, lines[i]);
                return 1;
            }
        }
        return 0;
    }
    qsort(lines, n, sizeof(char *), compare);
    FILE *out = stdout;
    if (output && !(out = fopen(output, "w"))) return util_perr(output);
    for (size_t i = 0; i < n; i++) {
        if (o_unique && i && compare(&lines[i - 1], &lines[i]) == 0) continue;
        fputs(lines[i], out);
        fputc('\n', out);
    }
    if (out != stdout) fclose(out);
    return rc;
}
