#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "settings/font_data.h"

struct sg_canvas {
    uint32_t *px;
    int w, h;
    int cx0, cy0, cx1, cy1;
};

enum sg_icon_size { SG_ICON_S = 0, SG_ICON_M = 1, SG_ICON_L = 2 };

void sg_init(struct sg_canvas *c, uint32_t *px, int w, int h);
void sg_clip(struct sg_canvas *c, int x, int y, int w, int h);
void sg_unclip(struct sg_canvas *c);

uint32_t sg_mix(uint32_t a, uint32_t b, uint32_t t);
uint32_t sg_lighten(uint32_t c, int amount);
int sg_luma(uint32_t c);

void sg_fill(struct sg_canvas *c, int x, int y, int w, int h, uint32_t color);
void sg_fill_a(struct sg_canvas *c, int x, int y, int w, int h, uint32_t color, uint32_t alpha);
void sg_vgrad(struct sg_canvas *c, int x, int y, int w, int h, uint32_t top, uint32_t bottom);
void sg_hgrad(struct sg_canvas *c, int x, int y, int w, int h, uint32_t left, uint32_t right);
void sg_rrect(struct sg_canvas *c, int x, int y, int w, int h, int r, uint32_t color, uint32_t alpha);
void sg_rrect_grad(struct sg_canvas *c, int x, int y, int w, int h, int r, uint32_t c0, uint32_t c1,
                   bool horizontal);
void sg_rrect_stroke(struct sg_canvas *c, int x, int y, int w, int h, int r, uint32_t color,
                     uint32_t alpha);
void sg_shadow(struct sg_canvas *c, int x, int y, int w, int h, int r, int spread, uint32_t alpha);
void sg_circle(struct sg_canvas *c, int cx, int cy, int r, uint32_t color, uint32_t alpha);
void sg_ring(struct sg_canvas *c, int cx, int cy, int r, int thick, uint32_t permille,
             uint32_t color, uint32_t track);

int sg_text(struct sg_canvas *c, int font, int x, int y, const char *s, uint32_t color);
int sg_text_w(int font, const char *s);
int sg_text_fit(struct sg_canvas *c, int font, int x, int y, int maxw, const char *s, uint32_t color);
void sg_text_center(struct sg_canvas *c, int font, int x, int y, int w, const char *s, uint32_t color);
int sg_font_h(int font);

void sg_icon(struct sg_canvas *c, int icon, int size, int x, int y, uint32_t color);
int sg_icon_px(int size);

void sg_bitfont(struct sg_canvas *c, int face, int x, int y, const char *s, uint32_t color, int size);
