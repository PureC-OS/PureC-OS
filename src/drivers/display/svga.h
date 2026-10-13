#pragma once
#include <stdbool.h>
#include <stdint.h>

bool svga_probe_only(void);
bool svga_init(uint32_t want_w, uint32_t want_h, uint8_t want_bpp);
bool svga_is_available(void);
void *svga_fb_virt(void);
uint32_t svga_width(void);
uint32_t svga_height(void);
uint32_t svga_pitch_pixels(void);
uint8_t svga_bpp(void);
uint64_t svga_fb_size(void);
uint32_t svga_caps(void);
void svga_update(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
bool svga_fill(uint32_t color, uint32_t x, uint32_t y, uint32_t w, uint32_t h);
bool svga_copy(uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h);
void svga_sync(void);
const char *svga_status_line(void);
