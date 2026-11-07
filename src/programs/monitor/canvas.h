#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "../../gfx/text.h"

struct canvas {
  uint32_t *px;
  int w;
  int h;
  int stride;
  int x0;
  int y0;
  int x1;
  int y1;
};

static inline void cv_init(struct canvas *c, uint32_t *px, int w, int h, int stride) {
  c->px = px;
  c->w = w;
  c->h = h;
  c->stride = stride;
  c->x0 = 0;
  c->y0 = 0;
  c->x1 = w;
  c->y1 = h;
}

static inline void cv_clip_reset(struct canvas *c) {
  c->x0 = 0;
  c->y0 = 0;
  c->x1 = c->w;
  c->y1 = c->h;
}

static inline void cv_clip(struct canvas *c, int x, int y, int w, int h) {
  cv_clip_reset(c);
  if (x > c->x0)
    c->x0 = x;
  if (y > c->y0)
    c->y0 = y;
  if (x + w < c->x1)
    c->x1 = x + w;
  if (y + h < c->y1)
    c->y1 = y + h;
}

static inline void cv_fill(struct canvas *c, int x, int y, int w, int h, uint32_t color) {
  int xa = x < c->x0 ? c->x0 : x;
  int ya = y < c->y0 ? c->y0 : y;
  int xb = x + w > c->x1 ? c->x1 : x + w;
  int yb = y + h > c->y1 ? c->y1 : y + h;
  if (xa >= xb || ya >= yb)
    return;
  for (int row = ya; row < yb; row++) {
    uint32_t *line = c->px + (size_t)row * (size_t)c->stride;
    for (int col = xa; col < xb; col++)
      line[col] = color;
  }
}

static inline int cv_inset(int radius, int dy) {
  int inset = 0;
  for (int x = 0; x < radius; x++) {
    int a = 2 * (radius - x) - 1;
    int b = 2 * (radius - dy) - 1;
    if (a * a + b * b > 4 * radius * radius)
      inset = x + 1;
    else
      break;
  }
  return inset;
}

static inline void cv_rrect(struct canvas *c, int x, int y, int w, int h, int r, uint32_t color) {
  if (r * 2 > h)
    r = h / 2;
  if (r * 2 > w)
    r = w / 2;
  if (r <= 0) {
    cv_fill(c, x, y, w, h, color);
    return;
  }
  for (int row = 0; row < h; row++) {
    int dy = row < r ? row : (row >= h - r ? h - 1 - row : r);
    int inset = dy < r ? cv_inset(r, dy) : 0;
    cv_fill(c, x + inset, y + row, w - 2 * inset, 1, color);
  }
}

static inline void cv_frame(struct canvas *c, int x, int y, int w, int h, int r,
                            uint32_t border, uint32_t fill) {
  cv_rrect(c, x, y, w, h, r, border);
  cv_rrect(c, x + 1, y + 1, w - 2, h - 2, r > 0 ? r - 1 : 0, fill);
}

static void cv_rect_cb(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color,
                       void *ctx) {
  cv_fill((struct canvas *)ctx, (int)x, (int)y, (int)w, (int)h, color);
}

static inline void cv_text(struct canvas *c, int x, int y, const char *text,
                           uint32_t color, int size) {
  if (x < 0 || y < 0)
    return;
  gfx_draw_text(text, (uint32_t)x, (uint32_t)y, color, (uint32_t)size, GFX_FONT_CLEAN,
                cv_rect_cb, c);
}

static inline int cv_text_width(const char *text, int size) {
  return (int)gfx_text_width(text, (uint32_t)size);
}

static inline void cv_text_fit(struct canvas *c, int x, int y, int max_width,
                               const char *text, uint32_t color, int size) {
  int fit = max_width / (size < 8 ? 8 : size);
  if (fit <= 0)
    return;
  char buffer[200];
  int n = 0;
  while (text[n] && n < fit && n < (int)sizeof(buffer) - 1) {
    buffer[n] = text[n];
    n++;
  }
  buffer[n] = 0;
  cv_text(c, x, y, buffer, color, size);
}

static inline uint32_t cv_mix(uint32_t a, uint32_t b, int t, int max) {
  if (t < 0)
    t = 0;
  if (t > max)
    t = max;
  uint32_t r = (((a >> 16) & 255) * (uint32_t)(max - t) + ((b >> 16) & 255) * (uint32_t)t) / (uint32_t)max;
  uint32_t g = (((a >> 8) & 255) * (uint32_t)(max - t) + ((b >> 8) & 255) * (uint32_t)t) / (uint32_t)max;
  uint32_t bl = ((a & 255) * (uint32_t)(max - t) + (b & 255) * (uint32_t)t) / (uint32_t)max;
  return (r << 16) | (g << 8) | bl;
}
