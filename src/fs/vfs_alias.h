#pragma once

#include <stdint.h>

int32_t vfs_alias_resolve(const char *path, char *out, uint32_t capacity);
