// Kernel symbol table for the module loader.
//
// Modules are linked with `ld -r`, so calls into the kernel stay as
// undefined symbols. The loader resolves them here. This list is the
// module ABI surface: adding an export is backwards compatible,
// removing one breaks already-built modules (bump KMOD_ABI_VERSION
// in that case).
#include "kmod.h"
#include "../diagnostics/klog.h"
#include "../../lib/string.h"

struct ksym_entry {
    const char *name;
    uint64_t addr;
};

#define KSYM(sym) {#sym, (uint64_t)(uintptr_t) & sym},

static const struct ksym_entry ksym_table[] = {
    // diagnostics
    KSYM(klog)
    KSYM(klogf)
    // freestanding libc subset (src/lib/string.c)
    KSYM(memset)
    KSYM(memcpy)
    KSYM(memcmp)
    KSYM(strlen)
    KSYM(strcmp)
    KSYM(strncmp)
    KSYM(strncpy)
};

uint64_t ksym_lookup(const char *name) {
    if (!name)
        return 0;
    for (unsigned i = 0; i < sizeof(ksym_table) / sizeof(ksym_table[0]); i++) {
        if (strcmp(ksym_table[i].name, name) == 0)
            return ksym_table[i].addr;
    }
    return 0;
}
