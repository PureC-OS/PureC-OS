#pragma once

#include <stdbool.h>
#include <stdint.h>

bool shadow_hash_for(const char *shadow, const char *name, char *out,
                     uint32_t capacity);
bool accounts_check(const char *shadow, const char *name,
                    const char *password);
