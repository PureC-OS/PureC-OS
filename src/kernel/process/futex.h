#pragma once

#include <stdint.h>

#define FUTEX_WAIT 0
#define FUTEX_WAKE 1
#define FUTEX_NO_TIMEOUT 0xFFFFFFFFu

int32_t futex_wait(uint64_t address_space, uint64_t address, uint32_t expected,
                   uint32_t timeout_ms);
int32_t futex_wake(uint64_t address_space, uint64_t address, int32_t count);
