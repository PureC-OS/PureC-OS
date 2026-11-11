#pragma once
#include <stdbool.h>
#include <stdint.h>

bool svga3d_start(void);
bool svga3d_active(void);
void *svga3d_screen_setup(uint32_t width, uint32_t height);
void svga3d_screen_teardown(void);
void svga3d_front_dirty(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
void svga3d_flush(void);
void svga3d_panic(void);
const char *svga3d_status(void);

#define SVGA3D_MODE_OPAQUE 0u
#define SVGA3D_MODE_MASK 1u
#define SVGA3D_MODE_BLEND 2u

bool svga3d_gpu(void);
void svga3d_set_frame_request(void (*fn)(uint32_t x, uint32_t y, uint32_t w,
                                         uint32_t h));
uint32_t svga3d_texture_create(uint64_t phys, uint32_t width, uint32_t height);
void svga3d_texture_destroy(uint32_t texture);
void svga3d_texture_update(uint32_t texture, uint32_t x, uint32_t y,
                           uint32_t w, uint32_t h);
void svga3d_frame_begin(void);
void svga3d_quad(uint32_t texture, int32_t dx, int32_t dy, uint32_t dw,
                 uint32_t dh, uint32_t sx, uint32_t sy, uint32_t sw,
                 uint32_t sh, uint32_t argb, uint32_t mode);
void svga3d_triangle(int32_t x0, int32_t y0, uint32_t c0, int32_t x1,
                     int32_t y1, uint32_t c1, int32_t x2, int32_t y2,
                     uint32_t c2);
void svga3d_frame_end(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
