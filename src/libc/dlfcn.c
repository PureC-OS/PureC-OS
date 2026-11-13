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

extern char __pc_text_start[] __attribute__((weak));
extern char __pc_text_end[] __attribute__((weak));
extern char __eh_frame_hdr_start[] __attribute__((weak));
extern char __eh_frame_hdr_end[] __attribute__((weak));

static int static_iterate_phdr(int (*callback)(struct dl_phdr_info *, size_t, void *), void *data)
{
    if (!__pc_text_start || !__pc_text_end || !__eh_frame_hdr_start || !__eh_frame_hdr_end)
        return 0;
    static Elf64_Phdr headers[2];
    headers[0].p_type = PT_LOAD;
    headers[0].p_flags = 5;
    headers[0].p_vaddr = (uint64_t)(uintptr_t)__pc_text_start;
    headers[0].p_paddr = headers[0].p_vaddr;
    headers[0].p_filesz = (uint64_t)(__pc_text_end - __pc_text_start);
    headers[0].p_memsz = headers[0].p_filesz;
    headers[0].p_align = 4096;
    headers[1].p_type = PT_GNU_EH_FRAME;
    headers[1].p_flags = 4;
    headers[1].p_vaddr = (uint64_t)(uintptr_t)__eh_frame_hdr_start;
    headers[1].p_paddr = headers[1].p_vaddr;
    headers[1].p_filesz = (uint64_t)(__eh_frame_hdr_end - __eh_frame_hdr_start);
    headers[1].p_memsz = headers[1].p_filesz;
    headers[1].p_align = 4;
    struct dl_phdr_info info;
    info.dlpi_addr = 0;
    info.dlpi_name = "";
    info.dlpi_phdr = headers;
    info.dlpi_phnum = 2;
    info.dlpi_adds = 1;
    info.dlpi_subs = 0;
    info.dlpi_tls_modid = 0;
    info.dlpi_tls_data = 0;
    return callback(&info, sizeof(info), data);
}

int dl_iterate_phdr(int (*callback)(struct dl_phdr_info *, size_t, void *), void *data)
{
    if (!__pc_dl_iterate_phdr)
        return static_iterate_phdr(callback, data);
    return __pc_dl_iterate_phdr(callback, data);
}
