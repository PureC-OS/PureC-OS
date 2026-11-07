#include <stdarg.h>
#include <stdio.h>

extern void (*__init_array_start[])(void);
extern void (*__init_array_end[])(void);

int __real_main(int argc, char **argv);

int __wrap_main(int argc, char **argv)
{
    for (void (**entry)(void) = __init_array_start; entry < __init_array_end; entry++)
        (*entry)();
    return __real_main(argc, argv);
}

int __real_curl_easy_setopt(void *handle, int option, ...);

int __wrap_curl_easy_setopt(void *handle, int option, ...)
{
    va_list args;
    va_start(args, option);
    unsigned long value = va_arg(args, unsigned long);
    va_end(args);
    int result = __real_curl_easy_setopt(handle, option, value);
    if (result != 0)
        fprintf(stderr, "curl_easy_setopt: option %d failed with code %d\n", option, result);
    return result;
}
