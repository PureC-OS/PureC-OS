#include "common.h"
#include "pager.h"

int main(int argc, char **argv)
{
    util_init(argc, argv);
    struct opts o;
    opts_begin(&o, argc, argv);
    bool number = false;
    int c;
    while ((c = opts_next(&o, "NRSFX")) != -1) {
        if (c == 'N') number = true;
        else if (c == 'L' && opts_long(&o, "help")) {
            util_usage("[-N] [FILE]...");
            return 0;
        } else if (c == 'R' || c == 'S' || c == 'F' || c == 'X') continue;
        else return util_bad_option(&o, c);
    }
    struct pager_text text = {0};
    int rc = 0;
    if (!o.count) o.operands[o.count++] = "-";
    for (int i = 0; i < o.count; i++) {
        FILE *f = util_fopen(o.operands[i]);
        if (!f) {
            rc = util_perr(o.operands[i]);
            continue;
        }
        pager_add_file(&text, f);
        util_fclose(f);
    }
    if (number) {
        for (int i = 0; i < text.count; i++) {
            char *line = malloc(strlen(text.lines[i]) + 32);
            sprintf(line, C_GRAY "%6d " C_RESET "%s", i + 1, text.lines[i]);
            free(text.lines[i]);
            text.lines[i] = line;
        }
    }
    const char *title = o.count == 1 && strcmp(o.operands[0], "-") ? o.operands[0] : "(stdin)";
    if (text.count) pager_show(&text, title, !strcmp(prog, "less"));
    pager_free(&text);
    return rc;
}
