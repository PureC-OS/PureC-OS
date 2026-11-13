#include "../lib/common.h"
#include <sys/statvfs.h>

int main(int argc, char **argv)
{
    util_init(argc, argv);
    struct opts o;
    opts_begin(&o, argc, argv);
    bool human = false, inodes = false;
    int c;
    while ((c = opts_next(&o, "hHikT")) != -1) {
        if (c == 'h' || c == 'H') human = true;
        else if (c == 'i') inodes = true;
        else if (c == 'k' || c == 'T') continue;
        else if (c == 'L' && opts_long(&o, "help")) { util_usage("[-hi] [FILE]..."); return 0; }
        else return util_bad_option(&o, c);
    }
    struct statvfs vfs;
    if (statvfs(o.count ? o.operands[0] : "/", &vfs) != 0) return util_perr("statvfs");
    bool color = util_color(1, NULL);
    if (inodes) {
        unsigned long long used = vfs.f_files - vfs.f_ffree;
        printf("%-12s %10s %10s %10s %5s %s\n", "Filesystem", "Inodes", "IUsed", "IFree", "IUse%", "Mounted on");
        printf("%-12s %10llu %10llu %10llu %4llu%% %s\n", "/dev/sda", (unsigned long long)vfs.f_files, used,
               (unsigned long long)vfs.f_ffree, vfs.f_files ? used * 100 / vfs.f_files : 0, "/");
        return 0;
    }
    unsigned long long bs = vfs.f_frsize;
    unsigned long long size = vfs.f_blocks * bs, avail = vfs.f_bavail * bs, used = (vfs.f_blocks - vfs.f_bfree) * bs;
    unsigned pct = size ? (unsigned)((used * 100 + size - 1) / size) : 0;
    printf("%-12s %9s %9s %9s %5s  %s\n", "Filesystem", human ? "Size" : "1K-blocks", "Used", "Avail", "Use%", "Mounted on");
    char a[32], b[32], d[32];
    if (human) {
        util_human(a, sizeof(a), size);
        util_human(b, sizeof(b), used);
        util_human(d, sizeof(d), avail);
    } else {
        snprintf(a, sizeof(a), "%llu", size / 1024);
        snprintf(b, sizeof(b), "%llu", used / 1024);
        snprintf(d, sizeof(d), "%llu", avail / 1024);
    }
    const char *pc = !color ? "" : pct >= 90 ? C_BOLD C_RED : pct >= 70 ? C_YELLOW : C_GREEN;
    printf("%-12s %9s %9s %9s %s%4u%%%s  %s\n", "/dev/sda", a, b, d, pc, pct, color ? C_RESET : "", "/");
    if (color && isatty(1)) {
        int width = 40, fill = (int)(pct * (unsigned)width / 100);
        printf("             %s", pc);
        for (int i = 0; i < width; i++) fputs(i < fill ? "\xe2\x96\x88" : C_GRAY "\xe2\x96\x91", stdout);
        printf(C_RESET "\n");
    }
    return 0;
}
