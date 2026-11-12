#include "../lib/common.h"

int main(int argc, char **argv)
{
    util_init(argc, argv);
    struct opts o;
    opts_begin(&o, argc, argv);
    long lines = 10, bytes = -1;
    bool quiet = false, verbose = false;
    int c;
    while ((c = opts_next(&o, "n:c:qv#")) != -1) {
        switch (c) {
        case 'n': if (!util_parse_long(o.arg, &lines)) return util_err("invalid number of lines: '%s'", o.arg); break;
        case 'c': if (!util_parse_long(o.arg, &bytes)) return util_err("invalid number of bytes: '%s'", o.arg); break;
        case '#': lines = atol(o.arg); break;
        case 'q': quiet = true; break;
        case 'v': verbose = true; break;
        case 'L': if (opts_long(&o, "help")) { util_usage("[-n LINES] [-c BYTES] [-qv] [FILE]..."); return 0; }
            return util_bad_option(&o, c);
        default: return util_bad_option(&o, c);
        }
    }
    if (!o.count) o.operands[o.count++] = "-";
    int rc = 0;
    for (int i = 0; i < o.count; i++) {
        FILE *f = util_fopen(o.operands[i]);
        if (!f) { rc = util_perr(o.operands[i]); continue; }
        if (verbose || (o.count > 1 && !quiet))
            printf("%s==> %s <==\n", i ? "\n" : "", strcmp(o.operands[i], "-") ? o.operands[i] : "standard input");
        if (bytes >= 0) {
            int ch;
            for (long n = 0; n < bytes && (ch = fgetc(f)) != EOF; n++) putchar(ch);
        } else if (lines >= 0) {
            int ch;
            long n = 0;
            while (n < lines && (ch = fgetc(f)) != EOF) {
                putchar(ch);
                if (ch == '\n') n++;
            }
        } else {
            char **ring = calloc((size_t)-lines, sizeof(char *));
            long keep = -lines, count = 0;
            char *line = NULL;
            size_t cap = 0;
            while (util_getline(&line, &cap, f) >= 0) {
                if (count >= keep) fputs(ring[count % keep], stdout);
                free(ring[count % keep]);
                ring[count % keep] = strdup(line);
                count++;
            }
            for (long k = 0; k < keep; k++) free(ring[k]);
            free(ring);
            free(line);
        }
        util_fclose(f);
    }
    return rc;
}
