#pragma once

#include <stdbool.h>
#include <stdint.h>

bool boot_get_kernel_image(const void **address, uint32_t *size);
bool boot_get_efi_loader(const void **address, uint32_t *size);
bool boot_get_module(const char *path, const void **address, uint64_t *size);
bool boot_get_module_by_index(uint64_t index, const void **address, uint64_t *size, const char **path);
void boot_log_modules(void);
uint64_t boot_get_module_count(void);
