#include "../lib/common.h"
#include <ctype.h>

static int expand(const char *s, unsigned char *out, int cap)
{
    int n = 0;
    while (*s && n < cap) {
        int c;
        if (s[0] == '[' && s[1] == ':') {
            const char *end = strstr(s, ":]");
            if (end) {
                size_t len = (size_t)(end - s - 2);
                for (int k = 0; k < 256 && n < cap; k++) {
                    bool in = false;
                    if (!strncmp(s + 2, "alpha", len)) in = isalpha(k);
                    else if (!strncmp(s + 2, "digit", len)) in = isdigit(k);
                    else if (!strncmp(s + 2, "alnum", len)) in = isalnum(k);
                    else if (!strncmp(s + 2, "upper", len)) in = isupper(k);
                    else if (!strncmp(s + 2, "lower", len)) in = islower(k);
                    else if (!strncmp(s + 2, "space", len)) in = isspace(k);
                    else if (!strncmp(s + 2, "blank", len)) in = k == ' ' || k == '\t';
                    else if (!strncmp(s + 2, "punct", len)) in = ispunct(k);
                    else if (!strncmp(s + 2, "print", len)) in = isprint(k);
                    else if (!strncmp(s + 2, "cntrl", len)) in = iscntrl(k);
                    else if (!strncmp(s + 2, "xdigit", len)) in = isxdigit(k);
                    if (in) out[n++] = (unsigned char)k;
                }
                s = end + 2;
                continue;
            }
        }
        if (*s == '\\' && s[1]) {
            s++;
            switch (*s) {
            case 'n': c = '\n'; break;
            case 't': c = '\t'; break;
            case 'r': c = '\r'; break;
            case '\\': c = '\\'; break;
            case '0': c = 0; break;
            default: c = (unsigned char)*s;
            }
            s++;
        } else c = (unsigned char)*s++;
        if (s[0] == '-' && s[1]) {
            int hi = (unsigned char)s[1];
            s += 2;
            for (int k = c; k <= hi && n < cap; k++) out[n++] = (unsigned char)k;
        } else out[n++] = (unsigned char)c;
    }
    return n;
}

int main(int argc, char **argv)
{
    util_init(argc, argv);
    struct opts o;
    opts_begin(&o, argc, argv);
    bool del = false, squeeze = false, complement = false;
    int c;
    while ((c = opts_next(&o, "dscC")) != -1) {
        if (c == 'd') del = true;
        else if (c == 's') squeeze = true;
        else if (c == 'c' || c == 'C') complement = true;
        else if (c == 'L' && opts_long(&o, "help")) { util_usage("[-dsc] SET1 [SET2]"); return 0; }
        else return util_bad_option(&o, c);
    }
    if (!o.count) return util_err("missing operand");
    unsigned char set1[1024], set2[1024];
    int n1 = expand(o.operands[0], set1, sizeof(set1));
    int n2 = o.count > 1 ? expand(o.operands[1], set2, sizeof(set2)) : 0;
    bool in1[256] = {false};
    for (int i = 0; i < n1; i++) in1[set1[i]] = true;
    if (complement)
        for (int k = 0; k < 256; k++) in1[k] = !in1[k];
    int map[256];
    for (int k = 0; k < 256; k++) map[k] = k;
    bool translate = !del && n2 > 0;
    if (translate) {
        if (complement) {
            for (int k = 0; k < 256; k++)
                if (in1[k]) map[k] = set2[n2 - 1];
        } else {
            for (int i = 0; i < n1; i++) map[set1[i]] = set2[i < n2 ? i : n2 - 1];
        }
    }
    bool squeeze_set[256] = {false};
    if (squeeze) {
        if (del || translate) for (int i = 0; i < n2; i++) squeeze_set[set2[i]] = true;
        else for (int k = 0; k < 256; k++) squeeze_set[k] = in1[k];
    }
    if (!del && !squeeze && !translate) return util_err("missing operand after '%s'", o.operands[0]);
    int ch, last = -1;
    while ((ch = getchar()) != EOF) {
        if (del && in1[ch]) continue;
        int out = in1[ch] ? map[ch] : ch;
        if (squeeze && out == last && squeeze_set[out]) continue;
        putchar(out);
        last = out;
    }
    return 0;
}
