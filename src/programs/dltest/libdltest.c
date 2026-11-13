#include <fcntl.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>

int dltest_counter = 100;
static int ctor_ran;

extern int dltest_exe_value __attribute__((weak));

__attribute__((constructor)) static void dltest_init(void)
{
    ctor_ran = 1;
    dltest_counter++;
}

__attribute__((destructor)) static void dltest_fini(void)
{
    int fd = open("/tmp/dltest.dtor", O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd >= 0)
    {
        write(fd, "dtor", 4);
        close(fd);
    }
}

int dltest_ctor_ran(void)
{
    return ctor_ran;
}

int dltest_add(int a, int b)
{
    return a + b;
}

size_t dltest_len(const char *text)
{
    return strlen(text);
}

int dltest_call(int (*callback)(int), int value)
{
    return callback(value) + 1;
}

int dltest_use_exe_symbol(void)
{
    return &dltest_exe_value ? dltest_exe_value : -1;
}

const char *dltest_name(void)
{
    return "libdltest";
}
