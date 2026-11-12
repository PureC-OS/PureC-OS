#include "../lib/common.h"

struct range { long lo, hi; };
static struct range ranges[64];
static int nranges;

static bool parse_list(const char *s)
{
    while (*s) {
        long lo = 1, hi = -1;
        char *end;
        if (*s != '-') {
            lo = strtol(s, &end, 10);
            if (end == s || lo < 1) return false;
            s = end;
            hi = lo;
        }
        if (*s == '-') {
            s++;
            if (*s >= '0' && *s <= '9') {
                hi = strtol(s, &end, 10);
                s = end;
            } else hi = -1;
        }
        if (nranges >= 64) return false;
        ranges[nranges].lo = lo;
        ranges[nranges].hi = hi;
        nranges++;
        if (*s == ',') s++;
        else if (*s) return false;
    }
    return nranges > 0;
}

static bool selected(long n)
{
    for (int i = 0; i < nranges; i++)
        if (n >= ranges[i].lo && (ranges[i].hi < 0 || n <= ranges[i].hi)) return true;
    return false;
}

int main(int argc, char **argv)
{
    util_init(argc, argv);
    struct opts o;
    opts_begin(&o, argc, argv);
    char mode = 0, delim = '\t';
    const char *out_delim = NULL;
    bool only_delimited = false, complement = false;
    int c;
    while ((c = opts_next(&o, "b:c:f:d:sn")) != -1) {
        switch (c) {
        case 'b': case 'c': case 'f':
            if (mode) return util_err("only one type of list may be specified");
            mode = (char)(c == 'b' ? 'c' : c);
            if (!parse_list(o.arg)) return util_err("invalid list '%s'", o.arg);
            break;
        case 'd': delim = o.arg[0]; break;
        case 's': only_delimited = true; break;
        case 'n': break;
        case 'L':
            if (opts_long(&o, "complement")) { complement = true; break; }
            if (opts_long(&o, "output-delimiter")) { out_delim = o.value ? o.value : ""; break; }
            if (opts_long(&o, "help")) { util_usage("-f LIST [-d DELIM] [-s] | -c LIST [FILE]..."); return 0; }
            return util_bad_option(&o, c);
        default: return util_bad_option(&o, c);
        }
    }
    if (!mode) return util_err("you must specify a list of bytes, characters, or fields");
    char od[2] = {delim, 0};
    if (!out_delim) out_delim = mode == 'f' ? od : "";
    if (!o.count) o.operands[o.count++] = "-";
    int rc = 0;
    for (int i = 0; i < o.count; i++) {
        FILE *f = util_fopen(o.operands[i]);
        if (!f) { rc = util_perr(o.operands[i]); continue; }
        char *line = NULL;
        size_t cap = 0;
        ssize_t len;
        while ((len = util_getline(&line, &cap, f)) >= 0) {
            if (len && line[len - 1] == '\n') line[--len] = 0;
            bool first = true;
            if (mode == 'c') {
                for (long k = 0; k < len; k++)
                    if (selected(k + 1) != complement) {
                        if (!first) fputs(out_delim, stdout);
                        putchar(line[k]);
                        if (*out_delim) first = false;
                    }
                putchar('\n');
                continue;
            }
            if (!strchr(line, delim)) {
                if (!only_delimited) puts(line);
                continue;
            }
            long field = 1;
            char *p = line;
            for (;;) {
                char *d = strchr(p, delim);
                size_t flen = d ? (size_t)(d - p) : strlen(p);
                if (selected(field) != complement) {
                    if (!first) fputs(out_delim, stdout);
                    fwrite(p, 1, flen, stdout);
                    first = false;
                }
                if (!d) break;
                p = d + 1;
                field++;
            }
            putchar('\n');
        }
        free(line);
        util_fclose(f);
    }
    return rc;
}
