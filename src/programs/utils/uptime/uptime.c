#include "../lib/common.h"
#include <time.h>
#include "libc/include/purec.h"

int main(int argc, char **argv)
{
    util_init(argc, argv);
    bool pretty = argc > 1 && !strcmp(argv[1], "-p");
    struct time_info info;
    if (pc_time_get(&info) != 0) return util_err("cannot read system time");
    unsigned long long secs = info.uptime_ms / 1000;
    unsigned long days = (unsigned long)(secs / 86400), hours = (unsigned long)(secs / 3600 % 24), mins = (unsigned long)(secs / 60 % 60);
    if (pretty) {
        printf("up");
        if (days) printf(" %lu day%s,", days, days == 1 ? "" : "s");
        if (hours) printf(" %lu hour%s,", hours, hours == 1 ? "" : "s");
        printf(" %lu minute%s\n", mins, mins == 1 ? "" : "s");
        return 0;
    }
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    char clock[16] = "";
    if (tm) strftime(clock, sizeof(clock), "%H:%M:%S", tm);
    struct process_monitor_info procs[128];
    int n = pc_process_list(procs, 128);
    printf(" %s up ", clock);
    if (days) printf("%lu day%s, ", days, days == 1 ? "" : "s");
    printf("%2lu:%02lu,  %d processes\n", hours, mins, n > 0 ? n : 0);
    return 0;
}
