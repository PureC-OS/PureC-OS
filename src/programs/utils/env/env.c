#include "../lib/common.h"

extern char **environ;

int main(int argc, char **argv)
{
    util_init(argc, argv);
    int i = 1;
    bool null_end = false;
    for (; i < argc; i++) {
        if (!strcmp(argv[i], "-i") || !strcmp(argv[i], "-")) clearenv();
        else if (!strcmp(argv[i], "-u") && i + 1 < argc) unsetenv(argv[++i]);
        else if (!strcmp(argv[i], "-0")) null_end = true;
        else if (!strcmp(argv[i], "--help")) { util_usage("[-i] [-u NAME] [NAME=VALUE]... [COMMAND [ARG]...]"); return 0; }
        else if (argv[i][0] == '-' && argv[i][1]) return util_err("invalid option '%s'", argv[i]);
        else break;
    }
    for (; i < argc && strchr(argv[i], '='); i++) {
        char *eq = strchr(argv[i], '=');
        *eq = 0;
        setenv(argv[i], eq + 1, 1);
        *eq = '=';
    }
    if (i < argc) {
        execvp(argv[i], argv + i);
        util_err("'%s': %s", argv[i], strerror(errno));
        return errno == ENOENT ? 127 : 126;
    }
    for (char **e = environ; e && *e; e++) {
        fputs(*e, stdout);
        putchar(null_end ? 0 : '\n');
    }
    return 0;
}
