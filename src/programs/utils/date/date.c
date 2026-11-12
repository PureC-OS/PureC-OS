#include "../lib/common.h"
#include <time.h>

int main(int argc, char **argv)
{
    util_init(argc, argv);
    bool utc = false;
    const char *format = NULL;
    time_t t = time(NULL);
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (a[0] == '+') format = a + 1;
        else if (!strcmp(a, "-u") || !strcmp(a, "--utc")) utc = true;
        else if (!strcmp(a, "-R")) format = "%a, %d %b %Y %H:%M:%S %z";
        else if (!strcmp(a, "-I")) format = "%Y-%m-%d";
        else if (!strcmp(a, "--iso-8601=seconds") || !strcmp(a, "-Iseconds")) format = "%Y-%m-%dT%H:%M:%S%z";
        else if (!strcmp(a, "-d") && i + 1 < argc) {
            const char *v = argv[++i];
            char *end;
            long s = strtol(v[0] == '@' ? v + 1 : v, &end, 10);
            if (*end) return util_err("invalid date '%s' (use @SECONDS)", v);
            t = (time_t)s;
        } else if (!strcmp(a, "--help")) { util_usage("[-uRI] [-d @SECONDS] [+FORMAT]"); return 0; }
        else return util_err("invalid argument '%s'", a);
    }
    struct tm *tm = utc ? gmtime(&t) : localtime(&t);
    if (!tm) return util_err("cannot convert time");
    char buf[512];
    if (!format) format = utc ? "%a %b %e %H:%M:%S UTC %Y" : "%a %b %e %H:%M:%S %Z %Y";
    if (!strftime(buf, sizeof(buf), format, tm) && *format) buf[0] = 0;
    puts(buf);
    return 0;
}
