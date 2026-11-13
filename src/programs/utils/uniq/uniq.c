#include "../lib/common.h"
#include <ctype.h>

static bool o_count, o_dups, o_uniq, o_icase;
static long skip_fields, skip_chars;

static const char *key(const char *s)
{
    for (long f = 0; f < skip_fields; f++) {
        while (*s && isspace((unsigned char)*s)) s++;
        while (*s && !isspace((unsigned char)*s)) s++;
    }
    for (long k = 0; k < skip_chars && *s; k++) s++;
    return s;
}

static bool same(const char *a, const char *b)
{
    a = key(a);
    b = key(b);
    return o_icase ? !strcasecmp(a, b) : !strcmp(a, b);
}

static void emit(FILE *out, const char *line, long count)
{
    if (!line) return;
    if (o_dups && count < 2) return;
    if (o_uniq && count > 1) return;
    if (o_count) fprintf(out, "%7ld %s\n", count, line);
    else fprintf(out, "%s\n", line);
}

int main(int argc, char **argv)
{
    util_init(argc, argv);
    struct opts o;
    opts_begin(&o, argc, argv);
    int c;
    while ((c = opts_next(&o, "cduif:s:")) != -1) {
        switch (c) {
        case 'c': o_count = true; break;
        case 'd': o_dups = true; break;
        case 'u': o_uniq = true; break;
        case 'i': o_icase = true; break;
        case 'f': skip_fields = atol(o.arg); break;
        case 's': skip_chars = atol(o.arg); break;
        case 'L': if (opts_long(&o, "help")) { util_usage("[-cdui] [-f N] [-s N] [INPUT [OUTPUT]]"); return 0; }
            return util_bad_option(&o, c);
        default: return util_bad_option(&o, c);
        }
    }
    FILE *in = util_fopen(o.count ? o.operands[0] : "-");
    if (!in) return util_perr(o.operands[0]);
    FILE *out = stdout;
    if (o.count > 1 && !(out = fopen(o.operands[1], "w"))) return util_perr(o.operands[1]);
    char *line = NULL, *prev = NULL;
    size_t cap = 0;
    ssize_t len;
    long count = 0;
    while ((len = util_getline(&line, &cap, in)) >= 0) {
        if (len && line[len - 1] == '\n') line[len - 1] = 0;
        if (prev && same(prev, line)) {
            count++;
            continue;
        }
        emit(out, prev, count);
        free(prev);
        prev = strdup(line);
        count = 1;
    }
    emit(out, prev, count);
    free(prev);
    free(line);
    util_fclose(in);
    if (out != stdout) fclose(out);
    return 0;
}
