#pragma once

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

int32_t kvformat(char *out, uint32_t capacity, const char *format, va_list args);
int32_t kformat(char *out, uint32_t capacity, const char *format, ...);
