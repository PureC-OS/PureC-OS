#pragma once

#include <stdbool.h>
#include <stdint.h>

bool shell_path_normalize(const char *base, const char *path,
                          char *output, uint32_t capacity);
