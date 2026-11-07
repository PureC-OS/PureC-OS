#pragma once

#define RTLD_LAZY 1
#define RTLD_NOW 2
#define RTLD_NOLOAD 4
#define RTLD_DEEPBIND 8
#define RTLD_GLOBAL 256
#define RTLD_LOCAL 0
#define RTLD_NODELETE 4096
#define RTLD_DEFAULT ((void *)0)
#define RTLD_NEXT ((void *)-1)

typedef struct {
    const char *dli_fname;
    void *dli_fbase;
    const char *dli_sname;
    void *dli_saddr;
} Dl_info;

#ifdef __cplusplus
extern "C" {
#endif

void *dlopen(const char *filename, int flags);
void *dlsym(void *handle, const char *name);
int dlclose(void *handle);
char *dlerror(void);
int dladdr(const void *address, Dl_info *info);

#ifdef __cplusplus
}
#endif
