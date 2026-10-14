#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool ksym_ready(void);
bool ksym_resolve(uint64_t addr, const char **name_out, uint64_t *offset_out);
