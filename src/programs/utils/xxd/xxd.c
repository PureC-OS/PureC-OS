#include "../lib/common.h"

int main(int argc, char **argv)
{
    util_init(argc, argv);
    struct opts o;
    opts_begin(&o, argc, argv);
    long cols = 16, limit = -1, skip = 0;
    bool plain = false, upper = false;
    const char *color_when = NULL;
    int c;
    while ((c = opts_next(&o, "c:l:s:puC")) != -1) {
        switch (c) {
        case 'c': cols = atol(o.arg); break;
        case 'l': limit = strtol(o.arg, NULL, 0); break;
        case 's': skip = strtol(o.arg, NULL, 0); break;
        case 'p': plain = true; break;
        case 'u': upper = true; break;
        case 'C': break;
        case 'L':
            if (opts_long(&o, "color")) { color_when = o.value ? o.value : "always"; break; }
            if (opts_long(&o, "help")) { util_usage("[-p] [-c COLS] [-l LEN] [-s OFFSET] [FILE]"); return 0; }
            return util_bad_option(&o, c);
        default: return util_bad_option(&o, c);
        }
    }
    if (cols < 1 || cols > 256) cols = 16;
    FILE *f = util_fopen(o.count ? o.operands[0] : "-");
    if (!f) return util_perr(o.operands[0]);
    bool color = util_color(1, color_when);
    for (long k = 0; k < skip && fgetc(f) != EOF; k++) {}
    unsigned char buf[256];
    long offset = skip, done = 0;
    const char *hexfmt = upper ? "%02X" : "%02x";
    for (;;) {
        long want = cols;
        if (limit >= 0 && limit - done < want) want = limit - done;
        if (want <= 0) break;
        size_t n = fread(buf, 1, (size_t)want, f);
        if (!n) break;
        if (plain) {
            for (size_t k = 0; k < n; k++) printf(hexfmt, buf[k]);
            putchar('\n');
        } else {
            printf(color ? C_YELLOW "%08lx" C_RESET ": " : "%08lx: ", offset);
            for (long k = 0; k < cols; k++) {
                if ((size_t)k < n) {
                    const char *col = !color ? "" : buf[k] == 0 ? C_GRAY : (buf[k] >= 32 && buf[k] < 127) ? C_GREEN : C_RED;
                    fputs(col, stdout);
                    printf(hexfmt, buf[k]);
                    if (color) fputs(C_RESET, stdout);
                } else fputs("  ", stdout);
                if (k % 2 == 1) putchar(' ');
            }
            putchar(' ');
            for (size_t k = 0; k < n; k++) {
                unsigned char ch = buf[k];
                bool printable = ch >= 32 && ch < 127;
                if (color) fputs(ch == 0 ? C_GRAY : printable ? C_GREEN : C_RED, stdout);
                putchar(printable ? ch : '.');
                if (color) fputs(C_RESET, stdout);
            }
            putchar('\n');
        }
        offset += (long)n;
        done += (long)n;
    }
    util_fclose(f);
    return 0;
}
