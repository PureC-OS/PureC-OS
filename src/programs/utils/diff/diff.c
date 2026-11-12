#include "../lib/common.h"
#include <ctype.h>

struct file_lines { char **v; int n; };

static bool load(const char *path, struct file_lines *f)
{
    FILE *in = util_fopen(path);
    if (!in) return false;
    f->v = NULL;
    f->n = 0;
    int cap = 0;
    char *line = NULL;
    size_t lcap = 0;
    ssize_t len;
    while ((len = util_getline(&line, &lcap, in)) >= 0) {
        if (len && line[len - 1] == '\n') line[len - 1] = 0;
        if (f->n == cap) {
            cap = cap ? cap * 2 : 256;
            f->v = realloc(f->v, sizeof(char *) * (size_t)cap);
        }
        f->v[f->n++] = strdup(line);
    }
    free(line);
    util_fclose(in);
    return true;
}

static bool o_icase, o_ws;

static bool eq(const char *a, const char *b)
{
    if (!o_ws) return o_icase ? !strcasecmp(a, b) : !strcmp(a, b);
    for (;;) {
        while (*a == ' ' || *a == '\t') a++;
        while (*b == ' ' || *b == '\t') b++;
        if (!*a || !*b) return *a == *b;
        char x = *a, y = *b;
        if (o_icase) { x = (char)tolower((unsigned char)x); y = (char)tolower((unsigned char)y); }
        if (x != y) return false;
        a++;
        b++;
    }
}

enum { OP_EQ, OP_DEL, OP_ADD };

int main(int argc, char **argv)
{
    util_init(argc, argv);
    struct opts o;
    opts_begin(&o, argc, argv);
    bool brief = false, unified = true;
    long context = 3;
    const char *color_when = NULL;
    int c;
    while ((c = opts_next(&o, "qsuU:iwbBNnr")) != -1) {
        switch (c) {
        case 'q': brief = true; break;
        case 'u': unified = true; break;
        case 'U': context = atol(o.arg); break;
        case 'i': o_icase = true; break;
        case 'w': case 'b': o_ws = true; break;
        case 's': case 'B': case 'N': case 'n': case 'r': break;
        case 'L':
            if (opts_long(&o, "color")) { color_when = o.value ? o.value : "always"; break; }
            if (opts_long(&o, "brief")) { brief = true; break; }
            if (opts_long(&o, "help")) { util_usage("[-qiw] [-U N] FILE1 FILE2"); return 0; }
            return util_bad_option(&o, c);
        default: return util_bad_option(&o, c);
        }
    }
    (void)unified;
    if (o.count != 2) return util_err("need exactly two files"), 2;
    struct file_lines a, b;
    if (!load(o.operands[0], &a)) return util_perr(o.operands[0]), 2;
    if (!load(o.operands[1], &b)) return util_perr(o.operands[1]), 2;
    bool color = util_color(1, color_when);
    int pre = 0;
    while (pre < a.n && pre < b.n && eq(a.v[pre], b.v[pre])) pre++;
    int suf = 0;
    while (suf < a.n - pre && suf < b.n - pre && eq(a.v[a.n - 1 - suf], b.v[b.n - 1 - suf])) suf++;
    int n = a.n - pre - suf, m = b.n - pre - suf;
    if (n == 0 && m == 0) return 0;
    if (brief) {
        printf("Files %s and %s differ\n", o.operands[0], o.operands[1]);
        return 1;
    }
    if ((long long)(n + 1) * (m + 1) > 40000000LL) return util_err("files too different to compare"), 2;
    unsigned short *lcs = malloc(sizeof(unsigned short) * (size_t)(n + 1) * (size_t)(m + 1));
    if (!lcs) return util_err("out of memory"), 2;
    #define L(i, j) lcs[(size_t)(i) * (size_t)(m + 1) + (size_t)(j)]
    for (int i = n; i >= 0; i--)
        for (int j = m; j >= 0; j--) {
            if (i == n || j == m) L(i, j) = 0;
            else if (eq(a.v[pre + i], b.v[pre + j])) L(i, j) = (unsigned short)(L(i + 1, j + 1) + 1);
            else L(i, j) = L(i + 1, j) > L(i, j + 1) ? L(i + 1, j) : L(i, j + 1);
        }
    int total = pre + suf + n + m;
    int *ops = malloc(sizeof(int) * (size_t)total), *ai = malloc(sizeof(int) * (size_t)total), *bi = malloc(sizeof(int) * (size_t)total);
    int k = 0;
    for (int i = 0; i < pre; i++) { ops[k] = OP_EQ; ai[k] = i; bi[k] = i; k++; }
    int i = 0, j = 0;
    while (i < n || j < m) {
        if (i < n && j < m && eq(a.v[pre + i], b.v[pre + j])) { ops[k] = OP_EQ; ai[k] = pre + i; bi[k] = pre + j; k++; i++; j++; }
        else if (j < m && (i == n || L(i, j + 1) >= L(i + 1, j))) { ops[k] = OP_ADD; ai[k] = pre + i; bi[k] = pre + j; k++; j++; }
        else { ops[k] = OP_DEL; ai[k] = pre + i; bi[k] = pre + j; k++; i++; }
    }
    for (int s = 0; s < suf; s++) { ops[k] = OP_EQ; ai[k] = a.n - suf + s; bi[k] = b.n - suf + s; k++; }
    free(lcs);
    if (color) printf(C_BOLD "--- %s\n+++ %s" C_RESET "\n", o.operands[0], o.operands[1]);
    else printf("--- %s\n+++ %s\n", o.operands[0], o.operands[1]);
    int p = 0;
    while (p < k) {
        while (p < k && ops[p] == OP_EQ) p++;
        if (p >= k) break;
        int start = p - (int)context;
        if (start < 0) start = 0;
        int end = p;
        for (;;) {
            while (end < k && ops[end] != OP_EQ) end++;
            int run = end;
            while (run < k && ops[run] == OP_EQ) run++;
            if (run < k && run - end <= 2 * context) { end = run; continue; }
            end = end + (int)context < k ? end + (int)context : k;
            if (end > run) end = run;
            break;
        }
        int a0 = ai[start], b0 = bi[start], ac = 0, bc = 0;
        for (int q = start; q < end; q++) {
            if (ops[q] != OP_ADD) ac++;
            if (ops[q] != OP_DEL) bc++;
        }
        if (color) printf(C_CYAN "@@ -%d,%d +%d,%d @@" C_RESET "\n", ac ? a0 + 1 : a0, ac, bc ? b0 + 1 : b0, bc);
        else printf("@@ -%d,%d +%d,%d @@\n", ac ? a0 + 1 : a0, ac, bc ? b0 + 1 : b0, bc);
        for (int q = start; q < end; q++) {
            if (ops[q] == OP_EQ) printf(" %s\n", a.v[ai[q]]);
            else if (ops[q] == OP_DEL) printf(color ? C_RED "-%s" C_RESET "\n" : "-%s\n", a.v[ai[q]]);
            else printf(color ? C_GREEN "+%s" C_RESET "\n" : "+%s\n", b.v[bi[q]]);
        }
        p = end;
    }
    return 1;
}
