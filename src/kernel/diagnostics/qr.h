#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define QR_MAX_VERSION 25
#define QR_MAX_DIM 117

bool qr_encode(const uint8_t *data, unsigned int len, uint8_t *cells,
               int *size_out);
