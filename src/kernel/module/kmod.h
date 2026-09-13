#pragma once
// Runtime loader for relocatable kernel modules (ET_REL, x86-64).
#include <stdint.h>
#include <stdbool.h>
#include "kmod_info.h"

// Early init: reserves the kernel module address area. Needs pmm+vmm+klog.
void kmod_init(void);

// Load a module image already in memory (e.g. from a Limine module).
// Returns 0 on success (kmod_info validated, init() ran).
int kmod_load_image(const char *name, const void *image, uint64_t size);

// Load "/bin/modules/<file>" passed by Limine (boot_get_module).
int kmod_load_limine(const char *limine_path);

// Resolve an exported symbol of a loaded module (defined FUNC/OBJECT).
// Returns NULL when the module/symbol is unknown.
void *kmod_get_symbol(const char *modname, const char *symname);

// Dump loaded modules via klog.
void kmod_list(void);

// Kernel symbol table (ksym.c): resolve an undefined module symbol
// against the static kernel. Returns 0 when unknown.
uint64_t ksym_lookup(const char *name);
