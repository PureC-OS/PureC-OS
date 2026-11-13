#pragma once

#include <stdint.h>

#define bswap_16(value) __builtin_bswap16(value)
#define bswap_32(value) __builtin_bswap32(value)
#define bswap_64(value) __builtin_bswap64(value)
