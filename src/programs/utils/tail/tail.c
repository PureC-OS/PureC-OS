#include "../lib/common.h"

static void tail_lines(FILE *f, long n, bool from_start)
{
    char *line = NULL;
    size_t cap = 0;
    if (from_start) {
        long k = 1;
        while (util_getline(&line, &cap, f) >= 0)
            if (k++ >= n) fputs(line, stdout);
        free(line);
        return;
    }
    if (n <= 0) return;
    char **ring = calloc((size_t)n, sizeof(char *));
    long count = 0;
    while (util_getline(&line, &cap, f) >= 0) {
        free(ring[count % n]);
        ring[count % n] = strdup(line);
        count++;
    }
    long start = count > n ? count - n : 0;
    for (long k = start; k < count; k++) fputs(ring[k % n], stdout);
    for (long k = 0; k < n; k++) free(ring[k]);
    free(ring);
    free(line);
}

static void tail_bytes(FILE *f, long n, bool from_start)
{
    size_t cap = 65536, len = 0;
    char *buf = malloc(cap);
    size_t r;
    while ((r = fread(buf + len, 1, cap - len, f)) > 0) {
        len += r;
        if (len == cap) {
            cap *= 2;
            buf = realloc(buf, cap);
        }
    }
    size_t start = from_start ? (size_t)(n > 0 ? n - 1 : 0) : (len > (size_t)n ? len - (size_t)n : 0);
    if (start < len) fwrite(buf + start, 1, len - start, stdout);
    free(buf);
}

int main(int argc, char **argv)
{
    util_init(argc, argv);
    struct opts o;
    opts_begin(&o, argc, argv);
    long count = 10;
    bool bytes = false, from_start = false, follow = false, quiet = false, verbose = false;
    int c;
    while ((c = opts_next(&o, "n:c:fFqvs:#")) != -1) {
        switch (c) {
        case 'n': case 'c':
            bytes = c == 'c';
            from_start = o.arg[0] == '+';
            if (!util_parse_long(o.arg[0] == '+' || o.arg[0] == '-' ? o.arg + 1 : o.arg, &count))
                return util_err("invalid number: '%s'", o.arg);
            break;
        case '#': count = atol(o.arg); break;
        case 'f': case 'F': follow = true; break;
        case 'q': quiet = true; break;
        case 'v': verbose = true; break;
        case 's': break;
        case 'L':
            if (opts_long(&o, "follow")) { follow = true; break; }
            if (opts_long(&o, "help")) { util_usage("[-n [+]N] [-c [+]N] [-f] [FILE]..."); return 0; }
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
        if (bytes) tail_bytes(f, count, from_start);
        else tail_lines(f, count, from_start);
        fflush(stdout);
        if (follow && i == o.count - 1 && f != stdin) {
            long pos = ftell(f);
            for (;;) {
                usleep(500000);
                struct stat st;
                if (stat(o.operands[i], &st) != 0) break;
                if (st.st_size < pos) pos = 0;
                if (st.st_size > pos) {
                    clearerr(f);
                    fseek(f, pos, SEEK_SET);
                    util_copy_stream(f, stdout);
                    fflush(stdout);
                    pos = ftell(f);
                }
            }
        }
        util_fclose(f);
    }
    return rc;
}
