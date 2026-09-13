#pragma once
// Kernel module protocol shared with out-of-tree modules.
//
// A loadable kernel module is a relocatable ELF (ET_REL, x86-64,
// built with `ld -r`) that exports exactly one well-known symbol:
//
//   const struct kmod_info kmod_info = {
//       .abi = KMOD_ABI_VERSION,
//       .name = "<module name>",
//       .version = 1,
//       .init = my_init,   // may be NULL
//       .fini = my_fini,   // may be NULL
//   };
//
// The loader copies SHF_ALLOC sections into kernel memory, applies
// RELA relocations (undefined symbols resolve against the kernel
// symbol table, see ksym.c), validates this descriptor and calls
// init(). init() returning non-zero fails the load.
#include <stdint.h>

#define KMOD_ABI_VERSION 1u

struct kmod_info {
    uint32_t abi;
    const char *name;
    uint32_t version;
    int (*init)(void);
    void (*fini)(void);
};
