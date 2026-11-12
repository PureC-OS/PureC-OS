#include "../lib/common.h"

int main(int argc, char **argv)
{
    util_init(argc, argv);
    char buf[64];
    puts(util_user(geteuid(), buf, sizeof(buf)));
    return 0;
}
