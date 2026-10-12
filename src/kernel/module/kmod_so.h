#pragma once

#include <stdbool.h>
#include <stdint.h>

#define KMOD_NAME_CAP 64

bool kmod_syms_ready(void);
bool kmod_find(const char *name, uint64_t *addr);
uint64_t kmod_load_so(const char *modname, const void *image, uint64_t size);
uint64_t kmod_get(const char *modname, const char *name);
