#pragma once
#include <stdbool.h>
#include <stdint.h>

bool thumb_decode_file(const char *path, uint32_t *dst, uint32_t w, uint32_t h);
