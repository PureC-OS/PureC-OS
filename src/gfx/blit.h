#pragma once

#include <stdbool.h>
#include <stdint.h>

#define BLIT_MAX_DIMENSION 16384u
#define BLIT_KEEP_ALPHA 1u
#define BLIT_ALPHA_OPAQUE 0xFF000000u

struct blit_span {
  uint32_t src_x;
  uint32_t src_y;
  uint32_t dst_x;
  uint32_t dst_y;
  uint32_t width;
  uint32_t height;
};

bool blit_clip(int32_t dst_x, int32_t dst_y, uint32_t width, uint32_t height,
               int32_t surface_x, int32_t surface_y, uint32_t surface_width,
               uint32_t surface_height, struct blit_span *out);
void blit_copy(uint32_t *dst, uint32_t dst_stride, const uint32_t *src,
               uint32_t src_stride, uint32_t width, uint32_t height,
               bool keep_alpha);
bool blit_source_bytes(uint32_t stride, uint32_t width, uint32_t height,
                       uint64_t *bytes);
