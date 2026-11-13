#include "../lib/common.h"
#include <sys/utsname.h>

int main(int argc, char **argv)
{
    util_init(argc, argv);
    struct opts o;
    opts_begin(&o, argc, argv);
    bool s = false, n = false, r = false, v = false, m = false, p = false, i = false, os = false;
    int c;
    while ((c = opts_next(&o, "asnrvmpio")) != -1) {
        switch (c) {
        case 'a': s = n = r = v = m = os = true; break;
        case 's': s = true; break;
        case 'n': n = true; break;
        case 'r': r = true; break;
        case 'v': v = true; break;
        case 'm': m = true; break;
        case 'p': p = true; break;
        case 'i': i = true; break;
        case 'o': os = true; break;
        case 'L': if (opts_long(&o, "help")) { util_usage("[-asnrvmpio]"); return 0; }
            return util_bad_option(&o, c);
        default: return util_bad_option(&o, c);
        }
    }
    if (!s && !n && !r && !v && !m && !p && !i && !os) s = true;
    struct utsname u;
    if (uname(&u) != 0) return util_perr("uname");
    bool first = true;
    #define OUT(cond, text) if (cond) { printf("%s%s", first ? "" : " ", text); first = false; }
    OUT(s, u.sysname);
    OUT(n, u.nodename);
    OUT(r, u.release);
    OUT(v, u.version);
    OUT(m, u.machine);
    OUT(p, u.machine);
    OUT(i, u.machine);
    OUT(os, "PureC");
    putchar('\n');
    return 0;
}
