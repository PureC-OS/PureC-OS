#include "common.h"

static bool o_number, o_nonblank, o_squeeze, o_ends, o_tabs, o_visible;
static long lineno;

static int cat_stream(FILE *in)
{
    if (!o_number && !o_nonblank && !o_squeeze && !o_ends && !o_tabs && !o_visible)
        return util_copy_stream(in, stdout);
    int c, prev = '\n';
    bool blank_run = false;
    while ((c = fgetc(in)) != EOF) {
        if (prev == '\n') {
            if (c == '\n') {
                if (o_squeeze && blank_run) continue;
                blank_run = true;
            } else blank_run = false;
            if (o_nonblank ? c != '\n' : o_number) printf("%6ld\t", ++lineno);
        }
        if (c == '\n') {
            if (o_ends) putchar('$');
            putchar('\n');
        } else if (c == '\t' && o_tabs) {
            fputs("^I", stdout);
        } else if (o_visible && c != '\t' && (c < 32 || c == 127)) {
            putchar('^');
            putchar(c == 127 ? '?' : c + 64);
        } else if (o_visible && c >= 128 && c < 160) {
            fputs("M-^", stdout);
            putchar(c - 128 + 64);
        } else {
            putchar(c);
        }
        prev = c;
    }
    return ferror(in) ? -1 : 0;
}

int main(int argc, char **argv)
{
    util_init(argc, argv);
    struct opts o;
    opts_begin(&o, argc, argv);
    int c;
    while ((c = opts_next(&o, "nbsETAvetu")) != -1) {
        switch (c) {
        case 'n': o_number = true; break;
        case 'b': o_nonblank = true; break;
        case 's': o_squeeze = true; break;
        case 'E': o_ends = true; break;
        case 'T': o_tabs = true; break;
        case 'v': o_visible = true; break;
        case 'A': o_visible = o_ends = o_tabs = true; break;
        case 'e': o_visible = o_ends = true; break;
        case 't': o_visible = o_tabs = true; break;
        case 'u': break;
        case 'L':
            if (opts_long(&o, "number")) { o_number = true; break; }
            if (opts_long(&o, "help")) { util_usage("[-nbsETAv] [FILE]..."); return 0; }
            return util_bad_option(&o, c);
        default: return util_bad_option(&o, c);
        }
    }
    if (!o.count) o.operands[o.count++] = "-";
    int rc = 0;
    for (int i = 0; i < o.count; i++) {
        FILE *f = util_fopen(o.operands[i]);
        if (!f) {
            rc = util_perr(o.operands[i]);
            continue;
        }
        if (cat_stream(f) < 0) rc = util_perr(o.operands[i]);
        util_fclose(f);
    }
    fflush(stdout);
    return rc;
}
