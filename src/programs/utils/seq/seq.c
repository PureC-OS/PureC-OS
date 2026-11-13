#include "../lib/common.h"

static int decimals(const char *s)
{
    const char *dot = strchr(s, '.');
    if (!dot) return 0;
    int n = 0;
    for (dot++; *dot >= '0' && *dot <= '9'; dot++) n++;
    return n;
}

int main(int argc, char **argv)
{
    util_init(argc, argv);
    const char *sep = "\n";
    const char *fmt = NULL;
    bool equal = false;
    char *nums[3];
    int count = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-s") && i + 1 < argc) sep = argv[++i];
        else if (!strcmp(argv[i], "-f") && i + 1 < argc) fmt = argv[++i];
        else if (!strcmp(argv[i], "-w")) equal = true;
        else if (!strcmp(argv[i], "--help")) { util_usage("[-w] [-s SEP] [-f FMT] [FIRST [INCREMENT]] LAST"); return 0; }
        else if (count < 3) nums[count++] = argv[i];
        else return util_err("extra operand '%s'", argv[i]);
    }
    if (!count) return util_err("missing operand");
    double first = 1, step = 1, last;
    int prec = 0;
    char *end;
    for (int i = 0; i < count; i++) {
        strtod(nums[i], &end);
        if (*end) return util_err("invalid floating point argument: '%s'", nums[i]);
        if (decimals(nums[i]) > prec) prec = decimals(nums[i]);
    }
    if (count == 1) last = strtod(nums[0], NULL);
    else if (count == 2) { first = strtod(nums[0], NULL); last = strtod(nums[1], NULL); }
    else { first = strtod(nums[0], NULL); step = strtod(nums[1], NULL); last = strtod(nums[2], NULL); }
    if (step == 0) return util_err("invalid zero increment value");
    int width = 0;
    if (equal) {
        char a[64], b[64];
        snprintf(a, sizeof(a), "%.*f", prec, first);
        snprintf(b, sizeof(b), "%.*f", prec, last);
        width = (int)(strlen(a) > strlen(b) ? strlen(a) : strlen(b));
    }
    bool started = false;
    for (long k = 0;; k++) {
        double v = first + (double)k * step;
        if ((step > 0 && v > last + 1e-9) || (step < 0 && v < last - 1e-9)) break;
        if (started) fputs(sep, stdout);
        started = true;
        if (fmt) printf(fmt, v);
        else if (equal) printf("%0*.*f", width, prec, v);
        else printf("%.*f", prec, v);
    }
    if (started) putchar('\n');
    return 0;
}
