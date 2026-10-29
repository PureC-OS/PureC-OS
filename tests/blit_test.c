#include "gfx/blit.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static void test_clip(void) {
  struct blit_span s;
  assert(blit_clip(10, 20, 30, 40, 0, 0, 100, 100, &s));
  assert(s.src_x == 0 && s.src_y == 0 && s.dst_x == 10 && s.dst_y == 20);
  assert(s.width == 30 && s.height == 40);

  assert(blit_clip(-5, -7, 30, 40, 0, 0, 100, 100, &s));
  assert(s.src_x == 5 && s.src_y == 7 && s.dst_x == 0 && s.dst_y == 0);
  assert(s.width == 25 && s.height == 33);

  assert(blit_clip(90, 95, 30, 40, 0, 0, 100, 100, &s));
  assert(s.src_x == 0 && s.src_y == 0 && s.dst_x == 90 && s.dst_y == 95);
  assert(s.width == 10 && s.height == 5);

  assert(blit_clip(110, 5, 4, 4, 100, 0, 50, 50, &s));
  assert(s.dst_x == 10 && s.dst_y == 5 && s.width == 4);

  assert(blit_clip(-10, -10, 200, 200, 20, 30, 40, 50, &s));
  assert(s.src_x == 30 && s.src_y == 40 && s.dst_x == 0 && s.dst_y == 0);
  assert(s.width == 40 && s.height == 50);

  assert(!blit_clip(100, 0, 10, 10, 0, 0, 100, 100, &s));
  assert(!blit_clip(0, 100, 10, 10, 0, 0, 100, 100, &s));
  assert(!blit_clip(-10, 0, 10, 10, 0, 0, 100, 100, &s));
  assert(!blit_clip(0, -10, 10, 10, 0, 0, 100, 100, &s));
  assert(!blit_clip(0, 0, 10, 10, 50, 50, 0, 10, &s));
  assert(blit_clip(-2147483647, -2147483647, 4000000000u, 4000000000u, 0, 0,
                   16, 16, &s));
  assert(s.width == 16 && s.height == 16);
}

static void test_copy(void) {
  uint32_t src[5 * 4];
  uint32_t dst[8 * 4];
  for (int i = 0; i < 20; i++)
    src[i] = (uint32_t)(0x00010203u + i);
  memset(dst, 0xAA, sizeof(dst));
  blit_copy(dst + 1, 8, src, 5, 5, 4, false);
  for (int y = 0; y < 4; y++) {
    for (int x = 0; x < 5; x++)
      assert(dst[y * 8 + 1 + x] == (src[y * 5 + x] | 0xFF000000u));
    assert(dst[y * 8] == 0xAAAAAAAAu);
    assert(dst[y * 8 + 6] == 0xAAAAAAAAu);
  }

  for (int i = 0; i < 20; i++)
    src[i] = 0x40000000u | (uint32_t)i;
  memset(dst, 0, sizeof(dst));
  blit_copy(dst, 8, src, 5, 5, 4, true);
  for (int y = 0; y < 4; y++)
    for (int x = 0; x < 5; x++)
      assert(dst[y * 8 + x] == src[y * 5 + x]);

  src[0] = 0x00123456u;
  blit_copy(dst, 8, src, 5, 1, 1, false);
  assert(dst[0] == 0xFF123456u);
  blit_copy(dst, 8, src, 5, 1, 1, true);
  assert(dst[0] == 0x00123456u);
}

static void test_bytes(void) {
  uint64_t bytes = 0;
  assert(blit_source_bytes(640, 640, 480, &bytes));
  assert(bytes == (479ull * 640 + 640) * 4);
  assert(blit_source_bytes(100, 64, 3, &bytes));
  assert(bytes == (2ull * 100 + 64) * 4);
  assert(!blit_source_bytes(63, 64, 3, &bytes));
  assert(!blit_source_bytes(64, 0, 3, &bytes));
  assert(!blit_source_bytes(64, 64, 0, &bytes));
  assert(!blit_source_bytes(20000, 20000, 2, &bytes));
  assert(!blit_source_bytes(16384, 16384, 16384, &bytes));
}

int main(void) {
  test_clip();
  test_copy();
  test_bytes();
  return 0;
}
