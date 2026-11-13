#include "../lib/common.h"

int main(int argc, char **argv)
{
    util_init(argc, argv);
    struct opts o;
    opts_begin(&o, argc, argv);
    bool append = false;
    int c;
    while ((c = opts_next(&o, "ai")) != -1) {
        if (c == 'a') append = true;
        else if (c == 'i') continue;
        else if (c == 'L' && opts_long(&o, "help")) { util_usage("[-a] [FILE]..."); return 0; }
        else return util_bad_option(&o, c);
    }
    FILE **files = calloc((size_t)o.count + 1, sizeof(FILE *));
    int rc = 0;
    for (int i = 0; i < o.count; i++) {
        files[i] = fopen(o.operands[i], append ? "a" : "w");
        if (!files[i]) rc = util_perr(o.operands[i]);
    }
    char buf[4096];
    ssize_t n;
    while ((n = read(0, buf, sizeof(buf))) > 0) {
        fwrite(buf, 1, (size_t)n, stdout);
        fflush(stdout);
        for (int i = 0; i < o.count; i++)
            if (files[i]) {
                fwrite(buf, 1, (size_t)n, files[i]);
                fflush(files[i]);
            }
    }
    for (int i = 0; i < o.count; i++)
        if (files[i]) fclose(files[i]);
    return rc;
}
