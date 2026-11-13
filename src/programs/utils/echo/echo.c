#include "../lib/common.h"

static bool emit_escaped(const char *s)
{
    for (; *s; s++) {
        if (*s != '\\' || !s[1]) { putchar(*s); continue; }
        s++;
        switch (*s) {
        case 'n': putchar('\n'); break;
        case 't': putchar('\t'); break;
        case 'r': putchar('\r'); break;
        case 'a': putchar('\a'); break;
        case 'b': putchar('\b'); break;
        case 'f': putchar('\f'); break;
        case 'v': putchar('\v'); break;
        case 'e': putchar(27); break;
        case '\\': putchar('\\'); break;
        case 'c': return false;
        case '0': {
            int v = 0;
            for (int k = 0; k < 3 && s[1] >= '0' && s[1] <= '7'; k++) v = v * 8 + (*++s - '0');
            putchar(v);
            break;
        }
        case 'x': {
            int v = 0, k = 0;
            while (k < 2 && ((s[1] >= '0' && s[1] <= '9') || ((s[1] | 32) >= 'a' && (s[1] | 32) <= 'f'))) {
                char h = *++s;
                v = v * 16 + (h <= '9' ? h - '0' : (h | 32) - 'a' + 10);
                k++;
            }
            putchar(v);
            break;
        }
        default: putchar('\\'); putchar(*s);
        }
    }
    return true;
}

int main(int argc, char **argv)
{
    util_init(argc, argv);
    bool newline = true, escapes = false;
    int i = 1;
    for (; i < argc; i++) {
        const char *a = argv[i];
        if (a[0] != '-' || !a[1]) break;
        bool valid = true;
        for (const char *p = a + 1; *p; p++)
            if (!strchr("neE", *p)) valid = false;
        if (!valid) break;
        for (const char *p = a + 1; *p; p++) {
            if (*p == 'n') newline = false;
            else if (*p == 'e') escapes = true;
            else escapes = false;
        }
    }
    for (int first = i; i < argc; i++) {
        if (i > first) putchar(' ');
        if (escapes) {
            if (!emit_escaped(argv[i])) return 0;
        } else fputs(argv[i], stdout);
    }
    if (newline) putchar('\n');
    return 0;
}
