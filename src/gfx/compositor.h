#pragma once

#include <stdbool.h>
#include <stdint.h>

bool compositor_window_create(uint32_t pid, uint32_t x, uint32_t y,
                              uint32_t width, uint32_t height);
bool compositor_window_update(uint32_t pid, uint32_t x, uint32_t y,
                              uint32_t width, uint32_t height);
void compositor_window_destroy(uint32_t pid);
void compositor_window_raise(uint32_t pid);
bool compositor_window_exists(uint32_t pid);

bool compositor_draw_rect(uint32_t pid, uint32_t x, uint32_t y, uint32_t w,
                          uint32_t h, uint32_t color);
bool compositor_draw_line(uint32_t pid, uint32_t x0, uint32_t y0, uint32_t x1,
                          uint32_t y1, uint32_t color);
bool compositor_scroll_rect_up(uint32_t pid, uint32_t x, uint32_t y, uint32_t w,
                               uint32_t h, uint32_t amount,
                               uint32_t fill_color);
bool compositor_begin_update(uint32_t pid);
bool compositor_end_update(uint32_t pid);

void compositor_cursor_move(int32_t x, int32_t y);
void compositor_cursor_show(bool visible);

bool compositor_present(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1);
