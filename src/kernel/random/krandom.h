#pragma once

#include <stdbool.h>
#include <stdint.h>

void krandom_init(void);
bool krandom_hardened(void);
void krandom_fill(void *buffer, uint32_t count);
void krandom_mix(const void *data, uint32_t count);
