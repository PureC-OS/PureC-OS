#include "../lib/common.h"

int main(int argc, char **argv)
{
    util_init(argc, argv);
    struct opts o;
    opts_begin(&o, argc, argv);
    bool silent = false, list = false;
    int c;
    while ((c = opts_next(&o, "sl")) != -1) {
        if (c == 's') silent = true;
        else if (c == 'l') list = true;
        else if (c == 'L' && opts_long(&o, "help")) { util_usage("[-sl] FILE1 [FILE2]"); return 0; }
        else return util_bad_option(&o, c);
    }
    if (o.count < 1) return util_err("missing operand"), 2;
    const char *n1 = o.operands[0], *n2 = o.count > 1 ? o.operands[1] : "-";
    FILE *a = util_fopen(n1), *b = util_fopen(n2);
    if (!a) return util_perr(n1), 2;
    if (!b) return util_perr(n2), 2;
    long byte = 1, line = 1;
    int rc = 0;
    for (;;) {
        int x = fgetc(a), y = fgetc(b);
        if (x == EOF && y == EOF) break;
        if (x == EOF || y == EOF) {
            if (!silent) fprintf(stderr, "%s: EOF on %s after byte %ld\n", prog, x == EOF ? n1 : n2, byte - 1);
            rc = 1;
            break;
        }
        if (x != y) {
            rc = 1;
            if (silent) break;
            if (list) printf("%ld %3o %3o\n", byte, x, y);
            else {
                printf("%s %s differ: byte %ld, line %ld\n", n1, n2, byte, line);
                break;
            }
        }
        if (x == '\n') line++;
        byte++;
    }
    util_fclose(a);
    util_fclose(b);
    return rc;
}
