#include "../lib/common.h"

int main(int argc, char **argv)
{
    util_init(argc, argv);
    char line[1024] = "";
    size_t len = 0;
    if (argc < 2) strcpy(line, "y");
    for (int i = 1; i < argc; i++) {
        len = strlen(line);
        snprintf(line + len, sizeof(line) - len, "%s%s", i > 1 ? " " : "", argv[i]);
    }
    len = strlen(line);
    line[len++] = '\n';
    char buf[8192];
    size_t fill = 0;
    while (fill + len <= sizeof(buf)) {
        memcpy(buf + fill, line, len);
        fill += len;
    }
    for (;;)
        if (write(1, buf, fill) <= 0) return 0;
}
