#pragma once
// PureC hosted libc: dlfcn stubs. Dynamic loading does not exist yet, so
// these exist only so TCC's AOT sources compile; any real use fails
// gracefully with dlerror(). tcc -run stays a phase-3 item.

#define RTLD_LAZY 1
#define RTLD_NOW 2
#define RTLD_GLOBAL 256
#define RTLD_LOCAL 0
#define RTLD_DEFAULT ((void *)0)

void *dlopen(const char *filename, int flags);
void *dlsym(void *handle, const char *name);
int dlclose(void *handle);
const char *dlerror(void);
