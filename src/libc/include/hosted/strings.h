#pragma once
// PureC hosted libc: strings.h, ASCII case-insensitive compare.
#include <stddef.h>

int strcasecmp(const char *left, const char *right);
int strncasecmp(const char *left, const char *right, size_t count);
