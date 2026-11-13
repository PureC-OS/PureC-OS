#include "../lib/common.h"

int main(int argc, char **argv)
{
    util_init(argc, argv);
    bool keep = argc > 1 && !strcmp(argv[1], "-x");
    fputs(keep ? "\x1b[H\x1b[2J" : "\x1b[H\x1b[2J\x1b[3J", stdout);
    return 0;
}
