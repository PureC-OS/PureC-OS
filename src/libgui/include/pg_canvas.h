#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "pg_font_data.h"

struct pg_canvas {
    uint32_t *px;
    int w, h;
    int cx0, cy0, cx1, cy1;
};

enum pg_icon_size { PG_ICON_S = 0, PG_ICON_M = 1, PG_ICON_L = 2 };

void pgc_init(struct pg_canvas *c, uint32_t *px, int w, int h);
void pgc_clip(struct pg_canvas *c, int x, int y, int w, int h);
void pgc_unclip(struct pg_canvas *c);
struct pg_clip { int x0, y0, x1, y1; };
struct pg_clip pgc_clip_push(struct pg_canvas *c, int x, int y, int w, int h);
void pgc_clip_pop(struct pg_canvas *c, struct pg_clip saved);

uint32_t pgc_mix(uint32_t a, uint32_t b, uint32_t t);
uint32_t pgc_lighten(uint32_t c, int amount);
int pgc_luma(uint32_t c);

void pgc_fill(struct pg_canvas *c, int x, int y, int w, int h, uint32_t color);
void pgc_fill_a(struct pg_canvas *c, int x, int y, int w, int h, uint32_t color, uint32_t alpha);
void pgc_vgrad(struct pg_canvas *c, int x, int y, int w, int h, uint32_t top, uint32_t bottom);
void pgc_hgrad(struct pg_canvas *c, int x, int y, int w, int h, uint32_t left, uint32_t right);
void pgc_rrect(struct pg_canvas *c, int x, int y, int w, int h, int r, uint32_t color, uint32_t alpha);
void pgc_rrect_grad(struct pg_canvas *c, int x, int y, int w, int h, int r, uint32_t c0, uint32_t c1,
                   bool horizontal);
void pgc_rrect_stroke(struct pg_canvas *c, int x, int y, int w, int h, int r, uint32_t color,
                     uint32_t alpha);
void pgc_shadow(struct pg_canvas *c, int x, int y, int w, int h, int r, int spread, uint32_t alpha);
void pgc_circle(struct pg_canvas *c, int cx, int cy, int r, uint32_t color, uint32_t alpha);
void pgc_ring(struct pg_canvas *c, int cx, int cy, int r, int thick, uint32_t permille,
             uint32_t color, uint32_t track);

int pgc_text(struct pg_canvas *c, int font, int x, int y, const char *s, uint32_t color);
int pgc_text_w(int font, const char *s);
int pgc_text_fit(struct pg_canvas *c, int font, int x, int y, int maxw, const char *s, uint32_t color);
void pgc_text_center(struct pg_canvas *c, int font, int x, int y, int w, const char *s, uint32_t color);
int pgc_font_h(int font);

void pgc_icon(struct pg_canvas *c, int icon, int size, int x, int y, uint32_t color);
void pgc_image(struct pg_canvas *c, int x, int y, int w, int h, const uint32_t *px, int r);
void pgc_line(struct pg_canvas *c, float x0, float y0, float x1, float y1, float width, uint32_t color);
float pgc_sin(float a);
float pgc_cos(float a);
int pgc_icon_px(int size);

void pgc_bitfont(struct pg_canvas *c, int face, int x, int y, const char *s, uint32_t color, int size);
