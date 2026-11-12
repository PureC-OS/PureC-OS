#include "../lib/common.h"

struct counts { unsigned long long lines, words, bytes, chars, longest; };

static bool o_l, o_w, o_c, o_m, o_L;

static void count(FILE *f, struct counts *r)
{
    memset(r, 0, sizeof(*r));
    int ch;
    bool in_word = false;
    unsigned long long width = 0;
    while ((ch = fgetc(f)) != EOF) {
        r->bytes++;
        if ((ch & 0xC0) != 0x80) r->chars++;
        if (ch == '\n') {
            r->lines++;
            if (width > r->longest) r->longest = width;
            width = 0;
        } else if (ch == '\t') width = (width / 8 + 1) * 8;
        else if ((ch & 0xC0) != 0x80) width++;
        bool space = ch == ' ' || ch == '\n' || ch == '\t' || ch == '\r' || ch == '\v' || ch == '\f';
        if (space) in_word = false;
        else if (!in_word) { in_word = true; r->words++; }
    }
    if (width > r->longest) r->longest = width;
}

static void show(const struct counts *r, const char *name, int w)
{
    if (o_l) printf("%*llu ", w, r->lines);
    if (o_w) printf("%*llu ", w, r->words);
    if (o_m) printf("%*llu ", w, r->chars);
    if (o_c) printf("%*llu ", w, r->bytes);
    if (o_L) printf("%*llu ", w, r->longest);
    if (name) printf("%s", name);
    putchar('\n');
}

int main(int argc, char **argv)
{
    util_init(argc, argv);
    struct opts o;
    opts_begin(&o, argc, argv);
    int c;
    while ((c = opts_next(&o, "lwcmL")) != -1) {
        if (c == 'l') o_l = true;
        else if (c == 'w') o_w = true;
        else if (c == 'c') o_c = true;
        else if (c == 'm') o_m = true;
        else if (c == 'L' && !o.name) o_L = true;
        else if (c == 'L' && opts_long(&o, "help")) { util_usage("[-lwcmL] [FILE]..."); return 0; }
        else return util_bad_option(&o, c);
    }
    if (!o_l && !o_w && !o_c && !o_m && !o_L) o_l = o_w = o_c = true;
    bool from_stdin = !o.count;
    if (from_stdin) o.operands[o.count++] = "-";
    struct counts *all = calloc((size_t)o.count, sizeof(struct counts));
    bool *ok = calloc((size_t)o.count, sizeof(bool));
    struct counts total = {0};
    int rc = 0;
    for (int i = 0; i < o.count; i++) {
        FILE *f = util_fopen(o.operands[i]);
        if (!f) { rc = util_perr(o.operands[i]); continue; }
        count(f, &all[i]);
        ok[i] = true;
        util_fclose(f);
        total.lines += all[i].lines;
        total.words += all[i].words;
        total.bytes += all[i].bytes;
        total.chars += all[i].chars;
        if (all[i].longest > total.longest) total.longest = all[i].longest;
    }
    int fields = o_l + o_w + o_c + o_m + o_L;
    int w = 1;
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "%llu", total.bytes > total.chars ? total.bytes : total.chars);
    w = (int)strlen(tmp);
    if (fields == 1 && o.count == 1) w = 1;
    else if (w < 3) w = 3;
    for (int i = 0; i < o.count; i++)
        if (ok[i]) show(&all[i], from_stdin ? NULL : o.operands[i], w);
    if (o.count > 1) show(&total, "total", w);
    return rc;
}
