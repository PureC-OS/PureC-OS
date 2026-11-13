#include "../lib/common.h"
#include <signal.h>
#include <ctype.h>

static const struct { const char *name; int num; } sigs[] = {
    {"HUP", 1}, {"INT", 2}, {"QUIT", 3}, {"ILL", 4}, {"TRAP", 5}, {"ABRT", 6}, {"BUS", 7}, {"FPE", 8},
    {"KILL", 9}, {"USR1", 10}, {"SEGV", 11}, {"USR2", 12}, {"PIPE", 13}, {"ALRM", 14}, {"TERM", 15},
    {"CHLD", 17}, {"CONT", 18}, {"STOP", 19}, {"TSTP", 20}, {"TTIN", 21}, {"TTOU", 22}};

static int lookup(const char *s)
{
    if (isdigit((unsigned char)*s)) return atoi(s);
    if (!strncasecmp(s, "SIG", 3)) s += 3;
    for (size_t i = 0; i < sizeof(sigs) / sizeof(sigs[0]); i++)
        if (!strcasecmp(s, sigs[i].name)) return sigs[i].num;
    return -1;
}

int main(int argc, char **argv)
{
    util_init(argc, argv);
    int sig = 15;
    int i = 1;
    if (i < argc && (!strcmp(argv[i], "-l") || !strcmp(argv[i], "-L"))) {
        if (i + 1 < argc) {
            int n = atoi(argv[i + 1]);
            for (size_t k = 0; k < sizeof(sigs) / sizeof(sigs[0]); k++)
                if (sigs[k].num == (n > 128 ? n - 128 : n)) { puts(sigs[k].name); return 0; }
            return util_err("unknown signal '%s'", argv[i + 1]);
        }
        for (size_t k = 0; k < sizeof(sigs) / sizeof(sigs[0]); k++)
            printf("%2d) SIG%-6s%s", sigs[k].num, sigs[k].name, (k % 5 == 4) ? "\n" : "  ");
        putchar('\n');
        return 0;
    }
    if (i < argc && !strcmp(argv[i], "--help")) { util_usage("[-s SIGNAL | -SIGNAL] PID... | -l"); return 0; }
    if (i < argc && !strcmp(argv[i], "-s") && i + 1 < argc) {
        sig = lookup(argv[i + 1]);
        i += 2;
    } else if (i < argc && argv[i][0] == '-' && argv[i][1]) {
        sig = lookup(argv[i] + 1);
        i++;
    }
    if (sig < 0) return util_err("invalid signal");
    if (i >= argc) return util_err("missing pid");
    int rc = 0;
    for (; i < argc; i++) {
        char *end;
        long pid = strtol(argv[i], &end, 10);
        if (*end) { rc = util_err("invalid pid '%s'", argv[i]); continue; }
        if (kill((int)pid, sig) != 0) rc = util_err("(%ld) - %s", pid, strerror(errno));
    }
    return rc;
}
