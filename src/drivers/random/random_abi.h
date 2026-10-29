#pragma once

#include <stdint.h>

#define PURECRYPT_RANDOM_ABI 1u
#define PURECRYPT_RANDOM_CHUNK 256u

#define RANDOM_SYM_ABI "purec_random_abi"
#define RANDOM_SYM_FILL "purec_random_fill"
#define RANDOM_SYM_RESEED "purec_random_reseed"

typedef uint32_t (*random_abi_fn)(void);
typedef int (*random_fill_fn)(void *buffer, uint32_t count);
typedef int (*random_reseed_fn)(const void *data, uint32_t count);
