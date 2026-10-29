#include "blit.h"
#include "../lib/string.h"

#define BLIT_MAX_SOURCE_BYTES (256ull * 1024ull * 1024ull)

bool blit_source_bytes(uint32_t stride, uint32_t width, uint32_t height,
                       uint64_t *bytes) {
  if (!width || !height || width > BLIT_MAX_DIMENSION ||
      height > BLIT_MAX_DIMENSION || stride < width ||
      stride > BLIT_MAX_DIMENSION)
    return false;
  uint64_t pixels = (uint64_t)(height - 1) * stride + width;
  uint64_t total = pixels * sizeof(uint32_t);
  if (total > BLIT_MAX_SOURCE_BYTES)
    return false;
  *bytes = total;
  return true;
}

bool blit_clip(int32_t dst_x, int32_t dst_y, uint32_t width, uint32_t height,
               int32_t surface_x, int32_t surface_y, uint32_t surface_width,
               uint32_t surface_height, struct blit_span *out) {
  int64_t x0 = dst_x;
  int64_t y0 = dst_y;
  int64_t x1 = x0 + (int64_t)width;
  int64_t y1 = y0 + (int64_t)height;
  int64_t sx0 = surface_x;
  int64_t sy0 = surface_y;
  int64_t sx1 = sx0 + (int64_t)surface_width;
  int64_t sy1 = sy0 + (int64_t)surface_height;
  int64_t ix0 = x0 > sx0 ? x0 : sx0;
  int64_t iy0 = y0 > sy0 ? y0 : sy0;
  int64_t ix1 = x1 < sx1 ? x1 : sx1;
  int64_t iy1 = y1 < sy1 ? y1 : sy1;
  if (ix0 >= ix1 || iy0 >= iy1)
    return false;
  out->src_x = (uint32_t)(ix0 - x0);
  out->src_y = (uint32_t)(iy0 - y0);
  out->dst_x = (uint32_t)(ix0 - sx0);
  out->dst_y = (uint32_t)(iy0 - sy0);
  out->width = (uint32_t)(ix1 - ix0);
  out->height = (uint32_t)(iy1 - iy0);
  return true;
}

static void copy_row_opaque(uint32_t *dst, const uint32_t *src,
                            uint32_t count) {
  const uint64_t pair_mask = ((uint64_t)BLIT_ALPHA_OPAQUE << 32) |
                             (uint64_t)BLIT_ALPHA_OPAQUE;
  uint32_t i = 0;
  for (; i + 2 <= count; i += 2) {
    uint64_t pair;
    memcpy(&pair, src + i, sizeof(pair));
    pair |= pair_mask;
    memcpy(dst + i, &pair, sizeof(pair));
  }
  if (i < count)
    dst[i] = src[i] | BLIT_ALPHA_OPAQUE;
}

void blit_copy(uint32_t *dst, uint32_t dst_stride, const uint32_t *src,
               uint32_t src_stride, uint32_t width, uint32_t height,
               bool keep_alpha) {
  for (uint32_t row = 0; row < height; row++) {
    uint32_t *d = dst + (uint64_t)row * dst_stride;
    const uint32_t *s = src + (uint64_t)row * src_stride;
    if (keep_alpha)
      memcpy(d, s, (uint64_t)width * sizeof(uint32_t));
    else
      copy_row_opaque(d, s, width);
  }
}
