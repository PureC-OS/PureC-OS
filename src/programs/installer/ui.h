#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "../../gfx/text.h"

struct ui_canvas {
    uint32_t *px;
    int32_t w, h;
};

enum ui_paint_mode { UI_PAINT_SOLID = 0, UI_PAINT_HGRAD, UI_PAINT_VGRAD };

struct ui_paint {
    enum ui_paint_mode mode;
    uint32_t c0, c1;
};

static inline struct ui_paint ui_solid(uint32_t c) {
    struct ui_paint p = {UI_PAINT_SOLID, c, c};
    return p;
}
static inline struct ui_paint ui_hgrad(uint32_t a, uint32_t b) {
    struct ui_paint p = {UI_PAINT_HGRAD, a, b};
    return p;
}
static inline struct ui_paint ui_vgrad(uint32_t a, uint32_t b) {
    struct ui_paint p = {UI_PAINT_VGRAD, a, b};
    return p;
}

uint32_t ui_mix(uint32_t a, uint32_t b, uint32_t t);
uint32_t ui_strlen(const char *s);
void ui_copy(char *dst, const char *src, uint32_t cap);

void ui_blend(struct ui_canvas *c, int32_t x, int32_t y, uint32_t color,
              uint32_t alpha);
void ui_fill(struct ui_canvas *c, int32_t x, int32_t y, int32_t w, int32_t h,
             uint32_t color, uint32_t alpha);
void ui_rrect(struct ui_canvas *c, int32_t x, int32_t y, int32_t w, int32_t h,
              int32_t r, struct ui_paint paint, uint32_t alpha);
void ui_rrect_stroke(struct ui_canvas *c, int32_t x, int32_t y, int32_t w,
                     int32_t h, int32_t r, int32_t thickness,
                     struct ui_paint paint, uint32_t alpha);
void ui_circle(struct ui_canvas *c, int32_t cx, int32_t cy, int32_t r,
               struct ui_paint paint, uint32_t alpha);
void ui_ring(struct ui_canvas *c, int32_t cx, int32_t cy, int32_t r_out,
             int32_t r_in, struct ui_paint paint, uint32_t alpha);
void ui_line(struct ui_canvas *c, int32_t x0, int32_t y0, int32_t x1,
             int32_t y1, int32_t radius, uint32_t color, uint32_t alpha);
void ui_shadow(struct ui_canvas *c, int32_t x, int32_t y, int32_t w, int32_t h,
               int32_t r, int32_t spread, int32_t offset_y, uint32_t alpha);
void ui_text(struct ui_canvas *c, int32_t x, int32_t y, const char *text,
             uint32_t color, uint32_t size, gfx_font_face_t face);
void ui_text_alpha(struct ui_canvas *c, int32_t x, int32_t y, const char *text,
                   uint32_t color, uint32_t size, gfx_font_face_t face,
                   uint32_t alpha);
void ui_text_bold(struct ui_canvas *c, int32_t x, int32_t y, const char *text,
                  uint32_t color, uint32_t size);
int32_t ui_text_w(const char *text, uint32_t size);
void ui_text_center(struct ui_canvas *c, int32_t x, int32_t w, int32_t y,
                    const char *text, uint32_t color, uint32_t size,
                    gfx_font_face_t face);
void ui_check(struct ui_canvas *c, int32_t cx, int32_t cy, int32_t scale,
              uint32_t color, uint32_t alpha);
void ui_cross(struct ui_canvas *c, int32_t cx, int32_t cy, int32_t scale,
              uint32_t color, uint32_t alpha);
void ui_spinner(struct ui_canvas *c, int32_t cx, int32_t cy, int32_t radius,
                uint32_t phase, uint32_t color);
void ui_background(struct ui_canvas *c, int32_t origin_x, int32_t origin_y,
                   int32_t screen_w, int32_t screen_h);
void ui_logo(struct ui_canvas *c, int32_t x, int32_t y, int32_t size);
