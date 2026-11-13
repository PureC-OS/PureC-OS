#include "../lib/common.h"

int main(int argc, char **argv)
{
    util_init(argc, argv);
    if (argc < 2) return util_err("missing operand");
    double total = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help")) { util_usage("NUMBER[smhd]..."); return 0; }
        char *end;
        double v = strtod(argv[i], &end);
        if (end == argv[i] || v < 0) return util_err("invalid time interval '%s'", argv[i]);
        if (*end == 'm') v *= 60;
        else if (*end == 'h') v *= 3600;
        else if (*end == 'd') v *= 86400;
        else if (*end && *end != 's') return util_err("invalid time interval '%s'", argv[i]);
        total += v;
    }
    unsigned long whole = (unsigned long)total;
    if (whole) sleep((unsigned)whole);
    unsigned long micros = (unsigned long)((total - (double)whole) * 1e6);
    if (micros) usleep((unsigned)micros);
    return 0;
}
