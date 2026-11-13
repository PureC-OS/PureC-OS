#include "../lib/common.h"

int main(int argc, char **argv)
{
    util_init(argc, argv);
    struct opts o;
    opts_begin(&o, argc, argv);
    char what = 0;
    bool names = false;
    int c;
    while ((c = opts_next(&o, "ugGnr")) != -1) {
        if (c == 'u' || c == 'g' || c == 'G') what = (char)c;
        else if (c == 'n') names = true;
        else if (c == 'r') continue;
        else if (c == 'L' && opts_long(&o, "help")) { util_usage("[-ugGn]"); return 0; }
        else return util_bad_option(&o, c);
    }
    uid_t uid = getuid();
    gid_t gid = getgid();
    char ub[64], gb[64];
    util_user(uid, ub, sizeof(ub));
    util_group(gid, gb, sizeof(gb));
    if (what == 'u') { if (names) puts(ub); else printf("%u\n", (unsigned)uid); return 0; }
    if (what == 'g' || what == 'G') { if (names) puts(gb); else printf("%u\n", (unsigned)gid); return 0; }
    printf("uid=%u(%s) gid=%u(%s) groups=%u(%s)\n", (unsigned)uid, ub, (unsigned)gid, gb, (unsigned)gid, gb);
    return 0;
}
