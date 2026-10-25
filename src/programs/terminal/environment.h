#pragma once

#include <stdbool.h>
#include <stdint.h>

bool shell_expand_environment(const char *input, char *output,
                              uint32_t capacity);
void shell_print_environment(void);
