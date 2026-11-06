#include "ui.h"

uint32_t ui_strlen(const char *s) {
    uint32_t n = 0;
    while (s && s[n])
        n++;
    return n;
}

void ui_copy(char *dst, const char *src, uint32_t cap) {
    if (!cap)
        return;
    uint32_t i = 0;
    while (src && src[i] && i + 1 < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

uint32_t ui_mix(uint32_t a, uint32_t b, uint32_t t) {
    if (t >= 255)
        return b & 0xFFFFFF;
    uint32_t out = 0;
    for (int shift = 0; shift <= 16; shift += 8) {
        uint32_t ca = (a >> shift) & 0xFF;
        uint32_t cb = (b >> shift) & 0xFF;
        out |= ((ca * (255 - t) + cb * t + 127) / 255) << shift;
    }
    return out;
}

void ui_blend(struct ui_canvas *c, int32_t x, int32_t y, uint32_t color,
              uint32_t alpha) {
    if (x < 0 || y < 0 || x >= c->w || y >= c->h || !alpha)
        return;
    uint32_t *p = &c->px[(int64_t)y * c->w + x];
    *p = alpha >= 255 ? (color & 0xFFFFFF) : ui_mix(*p, color, alpha);
}

void ui_fill(struct ui_canvas *c, int32_t x, int32_t y, int32_t w, int32_t h,
             uint32_t color, uint32_t alpha) {
    int32_t x0 = x < 0 ? 0 : x;
    int32_t y0 = y < 0 ? 0 : y;
    int32_t x1 = x + w > c->w ? c->w : x + w;
    int32_t y1 = y + h > c->h ? c->h : y + h;
    if (x0 >= x1 || y0 >= y1 || !alpha)
        return;
    color &= 0xFFFFFF;
    for (int32_t py = y0; py < y1; py++) {
        uint32_t *line = &c->px[(int64_t)py * c->w];
        if (alpha >= 255) {
            for (int32_t px = x0; px < x1; px++)
                line[px] = color;
        } else {
            for (int32_t px = x0; px < x1; px++)
                line[px] = ui_mix(line[px], color, alpha);
        }
    }
}

static int64_t sq(int64_t v) { return v * v; }

static uint32_t circle_cov(int32_t px, int32_t py, int32_t cx, int32_t cy,
                           int32_t r) {
    int64_t dx0 = px - cx, dx1 = px - cx + 1;
    int64_t dy0 = py - cy, dy1 = py - cy + 1;
    int64_t fx = sq(dx0) > sq(dx1) ? sq(dx0) : sq(dx1);
    int64_t fy = sq(dy0) > sq(dy1) ? sq(dy0) : sq(dy1);
    int64_t r2 = sq(r);
    if (fx + fy <= r2)
        return 255;
    int64_t nx = (dx0 <= 0 && dx1 >= 0) ? 0
                 : (sq(dx0) < sq(dx1) ? sq(dx0) : sq(dx1));
    int64_t ny = (dy0 <= 0 && dy1 >= 0) ? 0
                 : (sq(dy0) < sq(dy1) ? sq(dy0) : sq(dy1));
    if (nx + ny >= r2)
        return 0;
    uint32_t hits = 0;
    int64_t r8 = (int64_t)r * 8;
    for (int32_t j = 0; j < 4; j++) {
        int64_t sy = (int64_t)(py - cy) * 8 + 2 * j + 1;
        for (int32_t i = 0; i < 4; i++) {
            int64_t sx = (int64_t)(px - cx) * 8 + 2 * i + 1;
            if (sx * sx + sy * sy <= r8 * r8)
                hits++;
        }
    }
    return hits * 255 / 16;
}

static uint32_t rr_cov(int32_t px, int32_t py, int32_t x, int32_t y, int32_t w,
                       int32_t h, int32_t r) {
    if (w <= 0 || h <= 0 || px < x || py < y || px >= x + w || py >= y + h)
        return 0;
    if (r <= 0)
        return 255;
    if (r * 2 > w)
        r = w / 2;
    if (r * 2 > h)
        r = h / 2;
    if (px >= x + r && px < x + w - r)
        return 255;
    if (py >= y + r && py < y + h - r)
        return 255;
    int32_t cx = px < x + r ? x + r : x + w - r;
    int32_t cy = py < y + r ? y + r : y + h - r;
    return circle_cov(px, py, cx, cy, r);
}

static uint32_t paint_at(const struct ui_paint *p, int32_t px, int32_t py,
                         int32_t x, int32_t y, int32_t w, int32_t h) {
    if (p->mode == UI_PAINT_SOLID)
        return p->c0;
    if (p->mode == UI_PAINT_HGRAD) {
        int32_t span = w > 1 ? w - 1 : 1;
        int32_t t = (px - x) * 255 / span;
        return ui_mix(p->c0, p->c1, (uint32_t)(t < 0 ? 0 : t));
    }
    int32_t span = h > 1 ? h - 1 : 1;
    int32_t t = (py - y) * 255 / span;
    return ui_mix(p->c0, p->c1, (uint32_t)(t < 0 ? 0 : t));
}

void ui_rrect(struct ui_canvas *c, int32_t x, int32_t y, int32_t w, int32_t h,
              int32_t r, struct ui_paint paint, uint32_t alpha) {
    int32_t x0 = x < 0 ? 0 : x;
    int32_t y0 = y < 0 ? 0 : y;
    int32_t x1 = x + w > c->w ? c->w : x + w;
    int32_t y1 = y + h > c->h ? c->h : y + h;
    if (!alpha)
        return;
    for (int32_t py = y0; py < y1; py++) {
        for (int32_t px = x0; px < x1; px++) {
            uint32_t cov = rr_cov(px, py, x, y, w, h, r);
            if (!cov)
                continue;
            ui_blend(c, px, py, paint_at(&paint, px, py, x, y, w, h),
                     cov * alpha / 255);
        }
    }
}

void ui_rrect_stroke(struct ui_canvas *c, int32_t x, int32_t y, int32_t w,
                     int32_t h, int32_t r, int32_t t, struct ui_paint paint,
                     uint32_t alpha) {
    int32_t x0 = x < 0 ? 0 : x;
    int32_t y0 = y < 0 ? 0 : y;
    int32_t x1 = x + w > c->w ? c->w : x + w;
    int32_t y1 = y + h > c->h ? c->h : y + h;
    if (!alpha)
        return;
    for (int32_t py = y0; py < y1; py++) {
        bool inner_row = py >= y + t && py < y + h - t;
        for (int32_t px = x0; px < x1; px++) {
            if (inner_row && px >= x + t + r && px < x + w - t - r &&
                py >= y + t + r && py < y + h - t - r)
                continue;
            uint32_t outer = rr_cov(px, py, x, y, w, h, r);
            uint32_t inner = rr_cov(px, py, x + t, y + t, w - 2 * t,
                                    h - 2 * t, r > t ? r - t : 0);
            uint32_t cov = outer > inner ? outer - inner : 0;
            if (!cov)
                continue;
            ui_blend(c, px, py, paint_at(&paint, px, py, x, y, w, h),
                     cov * alpha / 255);
        }
    }
}

void ui_circle(struct ui_canvas *c, int32_t cx, int32_t cy, int32_t r,
               struct ui_paint paint, uint32_t alpha) {
    for (int32_t py = cy - r - 1; py <= cy + r; py++) {
        for (int32_t px = cx - r - 1; px <= cx + r; px++) {
            uint32_t cov = circle_cov(px, py, cx, cy, r);
            if (!cov)
                continue;
            ui_blend(c, px, py,
                     paint_at(&paint, px, py, cx - r, cy - r, 2 * r, 2 * r),
                     cov * alpha / 255);
        }
    }
}

void ui_ring(struct ui_canvas *c, int32_t cx, int32_t cy, int32_t r_out,
             int32_t r_in, struct ui_paint paint, uint32_t alpha) {
    for (int32_t py = cy - r_out - 1; py <= cy + r_out; py++) {
        for (int32_t px = cx - r_out - 1; px <= cx + r_out; px++) {
            uint32_t o = circle_cov(px, py, cx, cy, r_out);
            uint32_t i = r_in > 0 ? circle_cov(px, py, cx, cy, r_in) : 0;
            uint32_t cov = o > i ? o - i : 0;
            if (!cov)
                continue;
            ui_blend(c, px, py,
                     paint_at(&paint, px, py, cx - r_out, cy - r_out,
                              2 * r_out, 2 * r_out),
                     cov * alpha / 255);
        }
    }
}

void ui_line(struct ui_canvas *c, int32_t x0, int32_t y0, int32_t x1,
             int32_t y1, int32_t radius, uint32_t color, uint32_t alpha) {
    int32_t dx = x1 - x0, dy = y1 - y0;
    int32_t adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
    int32_t steps = adx > ady ? adx : ady;
    if (steps == 0)
        steps = 1;
    for (int32_t i = 0; i <= steps; i++) {
        int32_t px = x0 + dx * i / steps;
        int32_t py = y0 + dy * i / steps;
        ui_circle(c, px, py, radius, ui_solid(color), alpha);
    }
}

void ui_shadow(struct ui_canvas *c, int32_t x, int32_t y, int32_t w, int32_t h,
               int32_t r, int32_t spread, int32_t offset_y, uint32_t alpha) {
    int32_t sy = y + offset_y;
    ui_rrect(c, x, sy, w, h, r, ui_solid(0x000000), alpha);
    for (int32_t i = 1; i <= spread; i++) {
        int64_t k = spread - i + 1;
        uint32_t a = (uint32_t)(alpha * k * k / ((int64_t)spread * spread));
        ui_rrect_stroke(c, x - i, sy - i, w + 2 * i, h + 2 * i, r + i, 1,
                        ui_solid(0x000000), a);
    }
}

struct text_ctx {
    struct ui_canvas *c;
    uint32_t alpha;
};

static void text_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                      uint32_t color, void *ctx) {
    struct text_ctx *t = (struct text_ctx *)ctx;
    ui_fill(t->c, (int32_t)x, (int32_t)y, (int32_t)w, (int32_t)h, color,
            t->alpha);
}

void ui_text_alpha(struct ui_canvas *c, int32_t x, int32_t y, const char *text,
                   uint32_t color, uint32_t size, gfx_font_face_t face,
                   uint32_t alpha) {
    if (!text || x < 0 || y < 0)
        return;
    struct text_ctx ctx = {c, alpha};
    if (face == GFX_FONT_BOLD) {
        uint32_t off = (size + 8) / 16;
        for (uint32_t i = 0; i <= off; i += off ? off : 1)
            gfx_draw_text(text, (uint32_t)x + i, (uint32_t)y, color, size,
                          GFX_FONT_CLEAN, text_rect, &ctx);
        return;
    }
    gfx_draw_text(text, (uint32_t)x, (uint32_t)y, color, size, face, text_rect,
                  &ctx);
}

void ui_text(struct ui_canvas *c, int32_t x, int32_t y, const char *text,
             uint32_t color, uint32_t size, gfx_font_face_t face) {
    ui_text_alpha(c, x, y, text, color, size, face, 255);
}

void ui_text_bold(struct ui_canvas *c, int32_t x, int32_t y, const char *text,
                  uint32_t color, uint32_t size) {
    ui_text(c, x, y, text, color, size, GFX_FONT_CLEAN);
    ui_text(c, x + 1, y, text, color, size, GFX_FONT_CLEAN);
}

int32_t ui_text_w(const char *text, uint32_t size) {
    return (int32_t)gfx_text_width(text, size);
}

void ui_text_center(struct ui_canvas *c, int32_t x, int32_t w, int32_t y,
                    const char *text, uint32_t color, uint32_t size,
                    gfx_font_face_t face) {
    int32_t tw = ui_text_w(text, size);
    ui_text(c, x + (w - tw) / 2, y, text, color, size, face);
}

void ui_check(struct ui_canvas *c, int32_t cx, int32_t cy, int32_t s,
              uint32_t color, uint32_t alpha) {
    int32_t r = s / 9 < 1 ? 1 : s / 9;
    int32_t ax = cx - s * 3 / 10, ay = cy;
    int32_t bx = cx - s / 10, by = cy + s * 2 / 10;
    int32_t dx = cx + s * 3 / 10, dy = cy - s * 2 / 10;
    ui_line(c, ax, ay, bx, by, r, color, alpha);
    ui_line(c, bx, by, dx, dy, r, color, alpha);
}

void ui_cross(struct ui_canvas *c, int32_t cx, int32_t cy, int32_t s,
              uint32_t color, uint32_t alpha) {
    int32_t r = s / 9 < 1 ? 1 : s / 9;
    int32_t d = s * 25 / 100;
    ui_line(c, cx - d, cy - d, cx + d, cy + d, r, color, alpha);
    ui_line(c, cx - d, cy + d, cx + d, cy - d, r, color, alpha);
}

void ui_spinner(struct ui_canvas *c, int32_t cx, int32_t cy, int32_t radius,
                uint32_t phase, uint32_t color) {
    static const int16_t dir[8][2] = {{1024, 0},  {724, 724},   {0, 1024},
                                      {-724, 724}, {-1024, 0},  {-724, -724},
                                      {0, -1024}, {724, -724}};
    int32_t dot = radius / 5 < 2 ? 2 : radius / 5;
    uint32_t head = (phase / 2) % 8;
    for (uint32_t i = 0; i < 8; i++) {
        uint32_t age = (head + 8 - i) % 8;
        uint32_t alpha = 255 - age * 30;
        int32_t px = cx + dir[i][0] * radius / 1024;
        int32_t py = cy + dir[i][1] * radius / 1024;
        ui_circle(c, px, py, dot, ui_solid(color), alpha);
    }
}

static uint32_t glow(int64_t px, int64_t py, int64_t gx, int64_t gy,
                     int64_t radius, uint32_t amax) {
    int64_t r2 = radius * radius;
    int64_t d2 = sq(px - gx) + sq(py - gy);
    if (d2 >= r2)
        return 0;
    int64_t t = (r2 - d2) * 255 / r2;
    return (uint32_t)(amax * (t * t / 255) / 255);
}

void ui_background(struct ui_canvas *c, int32_t ox, int32_t oy, int32_t sw,
                   int32_t sh) {
    int64_t big = sw > sh ? sw : sh;
    for (int32_t y = 0; y < c->h; y++) {
        int32_t sy = oy + y;
        uint32_t base_t = sh > 1 ? (uint32_t)(sy * 255 / (sh - 1)) : 0;
        uint32_t base = ui_mix(0x0A0E1F, 0x1B1033, base_t);
        for (int32_t x = 0; x < c->w; x++) {
            int32_t sx = ox + x;
            uint32_t px = base;
            uint32_t a1 = glow(sx, sy, sw * 22 / 100, sh * 18 / 100,
                               big * 48 / 100, 90);
            if (a1)
                px = ui_mix(px, 0x3D5AFE, a1);
            uint32_t a2 = glow(sx, sy, sw * 86 / 100, sh * 90 / 100,
                               big * 52 / 100, 80);
            if (a2)
                px = ui_mix(px, 0xB44BFF, a2);
            uint32_t a3 = glow(sx, sy, sw * 84 / 100, sh * 8 / 100,
                               big * 28 / 100, 40);
            if (a3)
                px = ui_mix(px, 0x22D3EE, a3);
            c->px[(int64_t)y * c->w + x] = px;
        }
    }
}

void ui_logo(struct ui_canvas *c, int32_t x, int32_t y, int32_t size) {
    struct ui_paint grad = ui_vgrad(0x7C9CFF, 0xB388FF);
    ui_rrect(c, x, y, size, size, size / 4, grad, 255);
    int32_t cx = x + size / 2, cy = y + size / 2;
    int32_t r_out = size * 31 / 100;
    int32_t r_in = size * 20 / 100;
    ui_ring(c, cx, cy, r_out, r_in, ui_solid(0xFFFFFF), 255);
    int32_t gap = size * 11 / 100;
    for (int32_t py = cy - gap; py < cy + gap; py++) {
        for (int32_t px = cx; px < cx + r_out + 2; px++) {
            ui_blend(c, px, py, paint_at(&grad, px, py, x, y, size, size),
                     255);
        }
    }
    ui_circle(c, cx, cy, size * 7 / 100 < 2 ? 2 : size * 7 / 100,
              ui_solid(0xFFFFFF), 255);
}
