#pragma once

#include "include/puregui.h"

bool pg_internal_point_inside(int32_t x, int32_t y,
                              const struct pg_rect *rect);
struct pg_rect pg_internal_to_screen(const struct pg_window *window,
                                     struct pg_rect bounds);
void pg_internal_draw_text_clipped(uint32_t x, uint32_t y,
                                   const char *text, uint32_t color,
                                   uint32_t background,
                                   const struct pg_rect *clip);
void pg_internal_draw_text_sized_clipped(uint32_t x, uint32_t y,
                                         const char *text, uint32_t color,
                                         uint32_t background, uint32_t size,
                                         const struct pg_rect *clip);
bool pg_internal_update_registered_frame(struct pg_window *window);

#define PG_SYNC_MOVED 1
#define PG_SYNC_RESIZED 2
#define PG_SYNC_MINIMIZED 4

uint32_t pg_internal_sync_frame(struct pg_window *window, uint32_t state);
bool pg_internal_point_in_titlebar(const struct pg_window *window,
                                   int32_t x, int32_t y);
