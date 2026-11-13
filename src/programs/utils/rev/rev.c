#include "../lib/common.h"

int main(int argc, char **argv)
{
    util_init(argc, argv);
    if (argc == 1) argv[argc++] = "-";
    int rc = 0;
    for (int i = 1; i < argc; i++) {
        FILE *f = util_fopen(argv[i]);
        if (!f) { rc = util_perr(argv[i]); continue; }
        char *line = NULL;
        size_t cap = 0;
        ssize_t len;
        while ((len = util_getline(&line, &cap, f)) >= 0) {
            bool nl = len && line[len - 1] == '\n';
            if (nl) len--;
            ssize_t k = len;
            while (k > 0) {
                ssize_t s = k - 1;
                while (s > 0 && ((unsigned char)line[s] & 0xC0) == 0x80) s--;
                fwrite(line + s, 1, (size_t)(k - s), stdout);
                k = s;
            }
            putchar('\n');
        }
        free(line);
        util_fclose(f);
    }
    return rc;
}
