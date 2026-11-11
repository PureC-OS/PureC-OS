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
