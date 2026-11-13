#include "../lib/common.h"

int main(int argc, char **argv)
{
    util_init(argc, argv);
    if (argc < 2) return util_err("missing operand");
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help")) { util_usage("NAME..."); return 0; }
        char buf[UTIL_PATH];
        snprintf(buf, sizeof(buf), "%s", argv[i]);
        size_t len = strlen(buf);
        while (len > 1 && buf[len - 1] == '/') buf[--len] = 0;
        char *slash = strrchr(buf, '/');
        if (!slash) puts(".");
        else {
            while (slash > buf && slash[-1] == '/') slash--;
            if (slash == buf) puts("/");
            else { *slash = 0; puts(buf); }
        }
    }
    return 0;
}
