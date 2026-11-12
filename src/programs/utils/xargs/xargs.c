#include "../lib/common.h"
#include <ctype.h>

int main(int argc, char **argv)
{
    util_init(argc, argv);
    long max_args = 0;
    const char *replace = NULL;
    bool null_sep = false, verbose = false, no_empty = false;
    char delim = 0;
    int i = 1;
    for (; i < argc && argv[i][0] == '-'; i++) {
        if (!strcmp(argv[i], "--")) { i++; break; }
        if (!strcmp(argv[i], "-n") && i + 1 < argc) max_args = atol(argv[++i]);
        else if (!strncmp(argv[i], "-n", 2) && argv[i][2]) max_args = atol(argv[i] + 2);
        else if (!strcmp(argv[i], "-I") && i + 1 < argc) replace = argv[++i];
        else if (!strncmp(argv[i], "-I", 2)) replace = argv[i] + 2;
        else if (!strcmp(argv[i], "-0")) null_sep = true;
        else if (!strcmp(argv[i], "-t")) verbose = true;
        else if (!strcmp(argv[i], "-r")) no_empty = true;
        else if (!strcmp(argv[i], "-d") && i + 1 < argc) delim = argv[++i][0];
        else if (!strcmp(argv[i], "--help")) { util_usage("[-0rt] [-n N] [-I STR] [-d DELIM] [COMMAND [ARG]...]"); return 0; }
        else return util_err("invalid option '%s'", argv[i]);
    }
    char *default_cmd[] = {"echo", NULL};
    char **cmd = i < argc ? argv + i : default_cmd;
    int ncmd = i < argc ? argc - i : 1;
    char **items = NULL;
    int nitems = 0, cap = 0;
    char word[4096];
    size_t wl = 0;
    int ch;
    bool quoted = false;
    char quote = 0;
    bool have = false;
    while ((ch = getchar()) != EOF) {
        bool sep;
        if (null_sep) sep = ch == 0;
        else if (delim) sep = ch == delim;
        else if (replace) sep = ch == '\n';
        else if (quote) sep = false;
        else sep = isspace(ch);
        if (!null_sep && !delim && !replace) {
            if (quote) {
                if (ch == quote) { quote = 0; continue; }
            } else if (ch == '"' || ch == '\'') { quote = (char)ch; quoted = true; have = true; continue; }
            else if (ch == '\\') { ch = getchar(); if (ch == EOF) break; sep = false; }
        }
        if (sep) {
            if (wl || quoted || (replace && have)) {
                word[wl] = 0;
                if (nitems == cap) { cap = cap ? cap * 2 : 64; items = realloc(items, sizeof(char *) * (size_t)cap); }
                items[nitems++] = strdup(word);
            }
            wl = 0;
            quoted = false;
            have = false;
            continue;
        }
        have = true;
        if (wl + 1 < sizeof(word)) word[wl++] = (char)ch;
    }
    if (wl || quoted) {
        word[wl] = 0;
        if (nitems == cap) { cap = cap ? cap * 2 : 64; items = realloc(items, sizeof(char *) * (size_t)cap); }
        items[nitems++] = strdup(word);
    }
    if (!nitems && no_empty) return 0;
    int rc = 0;
    if (replace) {
        for (int k = 0; k < nitems; k++) {
            char **args = calloc((size_t)ncmd + 1, sizeof(char *));
            for (int a = 0; a < ncmd; a++) {
                const char *t = cmd[a];
                const char *hit = strstr(t, replace);
                if (!hit) { args[a] = (char *)t; continue; }
                size_t rl = strlen(replace), il = strlen(items[k]);
                char *out = malloc(strlen(t) * (il + 1) + 1);
                char *o = out;
                while ((hit = strstr(t, replace))) {
                    memcpy(o, t, (size_t)(hit - t)); o += hit - t;
                    memcpy(o, items[k], il); o += il;
                    t = hit + rl;
                }
                strcpy(o, t);
                args[a] = out;
            }
            if (verbose) { for (int a = 0; a < ncmd; a++) fprintf(stderr, "%s%s", a ? " " : "", args[a]); fputc('\n', stderr); }
            int r = util_exec_wait(args);
            if (r) rc = r == 127 ? 127 : 123;
            free(args);
        }
        return rc;
    }
    int per = max_args > 0 ? (int)max_args : (nitems ? nitems : 1);
    int k = 0;
    do {
        int take = nitems - k < per ? nitems - k : per;
        char **args = calloc((size_t)(ncmd + take) + 1, sizeof(char *));
        for (int a = 0; a < ncmd; a++) args[a] = cmd[a];
        for (int a = 0; a < take; a++) args[ncmd + a] = items[k + a];
        if (verbose) { for (int a = 0; a < ncmd + take; a++) fprintf(stderr, "%s%s", a ? " " : "", args[a]); fputc('\n', stderr); }
        int r = util_exec_wait(args);
        if (r) rc = r == 127 ? 127 : 123;
        free(args);
        k += take;
    } while (k < nitems);
    return rc;
}
