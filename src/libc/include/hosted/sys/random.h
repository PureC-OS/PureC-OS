#pragma once

#include <stddef.h>
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif
#define GRND_NONBLOCK 1
#define GRND_RANDOM 2

ssize_t getrandom(void *buffer, size_t length, unsigned int flags);
int getentropy(void *buffer, size_t length);
#ifdef __cplusplus
}
#endif
