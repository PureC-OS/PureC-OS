// PureC hosted libc: dlfcn stubs (see hosted/dlfcn.h). Dynamic loading
// does not exist; these only let TCC's AOT sources link.

#include "include/hosted/dlfcn.h"
#include <stddef.h>

static const char dlfn_error[] = "dynamic loading not supported on PureC OS";

void *dlopen(const char *filename, int flags) {
    (void)filename;
    (void)flags;
    return 0;
}

void *dlsym(void *handle, const char *name) {
    (void)handle;
    (void)name;
    return 0;
}

int dlclose(void *handle) {
    (void)handle;
    return -1;
}

const char *dlerror(void) {
    return dlfn_error;
}
