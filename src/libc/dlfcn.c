#include "include/hosted/dlfcn.h"
#include "include/hosted/link.h"
#include <stddef.h>

extern void *__pc_dl_open(const char *name, int flags, void *caller) __attribute__((weak));
extern void *__pc_dl_sym(void *handle, const char *name, void *caller) __attribute__((weak));
extern int __pc_dl_close(void *handle) __attribute__((weak));
extern const char *__pc_dl_error(void) __attribute__((weak));
extern int __pc_dl_addr(const void *addr, Dl_info *info) __attribute__((weak));
extern int __pc_dl_iterate_phdr(int (*callback)(struct dl_phdr_info *, size_t, void *), void *data) __attribute__((weak));

static const char static_error[] = "dynamic loading is unavailable in a statically linked program";
static int static_error_pending;

void *dlopen(const char *filename, int flags)
{
    if (!__pc_dl_open)
    {
        static_error_pending = 1;
        return 0;
    }
    return __pc_dl_open(filename, flags, __builtin_return_address(0));
}

void *dlsym(void *handle, const char *name)
{
    if (!__pc_dl_sym)
    {
        static_error_pending = 1;
        return 0;
    }
    return __pc_dl_sym(handle, name, __builtin_return_address(0));
}

int dlclose(void *handle)
{
    if (!__pc_dl_close)
    {
        static_error_pending = 1;
        return -1;
    }
    return __pc_dl_close(handle);
}

char *dlerror(void)
{
    if (!__pc_dl_error)
    {
        if (!static_error_pending)
            return 0;
        static_error_pending = 0;
        return (char *)static_error;
    }
    return (char *)__pc_dl_error();
}

int dladdr(const void *address, Dl_info *info)
{
    if (!__pc_dl_addr)
        return 0;
    return __pc_dl_addr(address, info);
}

int dl_iterate_phdr(int (*callback)(struct dl_phdr_info *, size_t, void *), void *data)
{
    if (!__pc_dl_iterate_phdr)
        return 0;
    return __pc_dl_iterate_phdr(callback, data);
}
