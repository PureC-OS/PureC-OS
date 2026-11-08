#include <dlfcn.h>
#include <fcntl.h>
#include <link.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int dltest_exe_value = 7;

static int failures;

static void check(const char *name, int ok)
{
    printf("dltest %-34s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok)
        failures++;
}

static int triple(int value)
{
    return value * 3;
}

static int phdr_counter(struct dl_phdr_info *info, size_t size, void *data)
{
    (void)size;
    int *state = (int *)data;
    state[0]++;
    if (info->dlpi_name && strstr(info->dlpi_name, "libdltest.so"))
        state[1] = 1;
    return 0;
}

int main(void)
{
    unlink("/tmp/dltest.dtor");

    void *self = dlopen(0, RTLD_NOW);
    check("dlopen(NULL) main handle", self != 0);
    check("dlsym main exe symbol", self && dlsym(self, "dltest_exe_value") == &dltest_exe_value);

    void *missing = dlopen("/lib/no-such-library.so", RTLD_NOW);
    const char *error = dlerror();
    check("dlopen missing -> NULL+dlerror", missing == 0 && error != 0 && error[0]);
    check("dlerror clears", dlerror() == 0);

    void *lib = dlopen("/lib/libdltest.so", RTLD_LAZY | RTLD_LOCAL);
    check("dlopen libdltest (lazy)", lib != 0);
    if (!lib)
    {
        printf("dltest dlerror: %s\n", dlerror());
        printf("dltest done failures=%d\n", failures + 1);
        return 1;
    }

    int (*add)(int, int) = (int (*)(int, int))dlsym(lib, "dltest_add");
    check("dlsym function", add != 0 && add(2, 3) == 5);
    int *counter = (int *)dlsym(lib, "dltest_counter");
    check("constructor ran before dlopen returned", counter && *counter == 101);
    int (*ctor_ran)(void) = (int (*)(void))dlsym(lib, "dltest_ctor_ran");
    check("static state set by constructor", ctor_ran && ctor_ran() == 1);
    check("dlsym data object", counter && (*counter)++ == 101 && *counter == 102);

    void *none = dlsym(lib, "dltest_nope");
    error = dlerror();
    check("dlsym missing -> NULL+dlerror", none == 0 && error != 0);

    size_t (*length)(const char *) = (size_t (*)(const char *))dlsym(lib, "dltest_len");
    check("lazy PLT call into libpurec.so", length && length("hello world") == 11);
    int (*caller)(int (*)(int), int) = (int (*)(int (*)(int), int))dlsym(lib, "dltest_call");
    check("callback into executable", caller && caller(triple, 14) == 43);
    int (*use_exe)(void) = (int (*)(void))dlsym(lib, "dltest_use_exe_symbol");
    check("library resolves exe symbol", use_exe && use_exe() == 7);

    check("RTLD_LOCAL hidden from RTLD_DEFAULT", dlsym(RTLD_DEFAULT, "dltest_add") == 0);
    dlerror();

    Dl_info info;
    memset(&info, 0, sizeof(info));
    check("dladdr", add && dladdr((void *)add, &info) != 0 && info.dli_fname &&
                        strstr(info.dli_fname, "libdltest") && info.dli_sname &&
                        strcmp(info.dli_sname, "dltest_add") == 0);

    int phdr_state[2] = {0, 0};
    dl_iterate_phdr(phdr_counter, phdr_state);
    check("dl_iterate_phdr sees objects", phdr_state[0] >= 3 && phdr_state[1] == 1);

    void *again = dlopen("/lib/libdltest.so", RTLD_NOW);
    check("second dlopen same handle", again == lib);
    check("dlclose #1 keeps library", dlclose(again) == 0 && dlsym(lib, "dltest_add") == (void *)add);
    check("dtor not run yet", access("/tmp/dltest.dtor", F_OK) != 0);
    check("dlclose #2 unloads", dlclose(lib) == 0);
    check("destructor ran on last dlclose", access("/tmp/dltest.dtor", F_OK) == 0);
    unlink("/tmp/dltest.dtor");

    void *global = dlopen("/lib/libdltest.so", RTLD_NOW | RTLD_GLOBAL);
    check("reopen after unload", global != 0);
    check("RTLD_GLOBAL visible via RTLD_DEFAULT", dlsym(RTLD_DEFAULT, "dltest_add") != 0);
    counter = (int *)dlsym(global, "dltest_counter");
    check("fresh instance after unload", counter && *counter == 101);

    void *dependent = dlopen("/lib/libdltest2.so", RTLD_NOW);
    check("dlopen with DT_NEEDED on libdltest", dependent != 0);
    int (*twice)(int) = dependent ? (int (*)(int))dlsym(dependent, "dltest2_twice") : 0;
    check("dependent resolves into dependency", twice && twice(21) == 42);
    check("dlclose dependent", dependent && dlclose(dependent) == 0);
    check("dependency still loaded (global)", dlsym(global, "dltest_add") != 0);
    check("dlclose global", dlclose(global) == 0);

    void *noload = dlopen("/lib/libdltest.so", RTLD_NOW | RTLD_NOLOAD);
    check("RTLD_NOLOAD after unload", noload == 0);
    dlerror();

    printf("dltest done failures=%d\n", failures);
    return failures ? 1 : 0;
}
