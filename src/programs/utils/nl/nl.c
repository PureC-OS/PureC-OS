#include "../lib/common.h"

int main(int argc, char **argv)
{
    util_init(argc, argv);
    struct opts o;
    opts_begin(&o, argc, argv);
    char style = 't';
    long start = 1, incr = 1, width = 6;
    const char *sep = "\t";
    int c;
    while ((c = opts_next(&o, "b:v:i:w:s:")) != -1) {
        switch (c) {
        case 'b': style = o.arg[0]; break;
        case 'v': start = atol(o.arg); break;
        case 'i': incr = atol(o.arg); break;
        case 'w': width = atol(o.arg); break;
        case 's': sep = o.arg; break;
        case 'L': if (opts_long(&o, "help")) { util_usage("[-b a|t|n] [-v START] [-i INCR] [-w WIDTH] [-s SEP] [FILE]..."); return 0; }
            return util_bad_option(&o, c);
        default: return util_bad_option(&o, c);
        }
    }
    if (!o.count) o.operands[o.count++] = "-";
    long n = start;
    int rc = 0;
    for (int i = 0; i < o.count; i++) {
        FILE *f = util_fopen(o.operands[i]);
        if (!f) { rc = util_perr(o.operands[i]); continue; }
        char *line = NULL;
        size_t cap = 0;
        ssize_t len;
        while ((len = util_getline(&line, &cap, f)) >= 0) {
            bool empty = len == 0 || (len == 1 && line[0] == '\n');
            if (style == 'a' || (style == 't' && !empty)) {
                printf("%*ld%s%s", (int)width, n, sep, line);
                n += incr;
            } else printf("%*s%s", (int)(width + (long)strlen(sep)), "", line);
            if (len && line[len - 1] != '\n') putchar('\n');
        }
        free(line);
        util_fclose(f);
    }
    return rc;
}
