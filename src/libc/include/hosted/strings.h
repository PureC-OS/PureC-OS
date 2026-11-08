#pragma once
// PureC hosted libc: strings.h, ASCII case-insensitive compare.
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif
int strcasecmp(const char *left, const char *right);
int strncasecmp(const char *left, const char *right, size_t count);
#ifdef __cplusplus
}
#endif
