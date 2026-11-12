#include "../lib/common.h"
#include "libc/include/purec.h"

int main(int argc, char **argv)
{
    util_init(argc, argv);
    char unit = 'k';
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--human")) unit = 'h';
        else if (!strcmp(argv[i], "-b")) unit = 'b';
        else if (!strcmp(argv[i], "-k")) unit = 'k';
        else if (!strcmp(argv[i], "-m")) unit = 'm';
        else if (!strcmp(argv[i], "-g")) unit = 'g';
        else if (!strcmp(argv[i], "--help")) { util_usage("[-h|-b|-k|-m|-g]"); return 0; }
        else return util_err("invalid option '%s'", argv[i]);
    }
    struct memory_monitor_info m;
    if (!pc_memory_info(&m)) return util_err("cannot read memory information");
    unsigned long long vals[4] = {m.total_bytes, m.used_bytes, m.total_bytes > m.used_bytes + m.available_bytes ? 0 : m.available_bytes, m.available_bytes};
    vals[2] = m.total_bytes - m.used_bytes;
    bool color = util_color(1, NULL);
    printf("%s%16s %11s %11s %11s%s\n", color ? C_BOLD : "", "total", "used", "free", "available", color ? C_RESET : "");
    printf("%-5s", "Mem:");
    for (int i = 0; i < 4; i++) {
        char buf[32];
        if (unit == 'h') util_human(buf, sizeof(buf), vals[i]);
        else if (unit == 'b') snprintf(buf, sizeof(buf), "%llu", vals[i]);
        else if (unit == 'm') snprintf(buf, sizeof(buf), "%llu", vals[i] >> 20);
        else if (unit == 'g') snprintf(buf, sizeof(buf), "%llu", vals[i] >> 30);
        else snprintf(buf, sizeof(buf), "%llu", vals[i] >> 10);
        printf(" %11s", buf);
    }
    putchar('\n');
    if (color && isatty(1) && m.total_bytes) {
        int width = 46, fill = (int)(m.used_bytes * (unsigned long long)width / m.total_bytes);
        unsigned pct = (unsigned)(m.used_bytes * 100 / m.total_bytes);
        const char *col = pct >= 90 ? C_RED : pct >= 70 ? C_YELLOW : C_GREEN;
        printf("     %s", col);
        for (int i = 0; i < width; i++) fputs(i < fill ? "\xe2\x96\x88" : C_GRAY "\xe2\x96\x91", stdout);
        printf(C_RESET " %u%%\n", pct);
    }
    return 0;
}
