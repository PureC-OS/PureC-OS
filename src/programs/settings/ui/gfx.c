#include "settings/gfx.h"
#include "../../../gfx/text.h"

static inline int imin(int a, int b) { return a < b ? a : b; }
static inline int imax(int a, int b) { return a > b ? a : b; }

void sg_init(struct sg_canvas *c, uint32_t *px, int w, int h)
{
    c->px = px;
    c->w = w;
    c->h = h;
    sg_unclip(c);
}

void sg_clip(struct sg_canvas *c, int x, int y, int w, int h)
{
    c->cx0 = imax(0, x);
    c->cy0 = imax(0, y);
    c->cx1 = imin(c->w, x + w);
    c->cy1 = imin(c->h, y + h);
}

void sg_unclip(struct sg_canvas *c)
{
    c->cx0 = 0;
    c->cy0 = 0;
    c->cx1 = c->w;
    c->cy1 = c->h;
}

uint32_t sg_mix(uint32_t a, uint32_t b, uint32_t t)
{
    if (t >= 255)
        return b & 0xffffff;
    if (t == 0)
        return a & 0xffffff;
    uint32_t it = 255 - t;
    uint32_t r = (((a >> 16) & 255) * it + ((b >> 16) & 255) * t + 127) / 255;
    uint32_t g = (((a >> 8) & 255) * it + ((b >> 8) & 255) * t + 127) / 255;
    uint32_t bl = ((a & 255) * it + (b & 255) * t + 127) / 255;
    return (r << 16) | (g << 8) | bl;
}

uint32_t sg_lighten(uint32_t c, int amount)
{
    if (amount >= 0)
        return sg_mix(c, 0xffffff, (uint32_t)imin(amount, 255));
    return sg_mix(c, 0x000000, (uint32_t)imin(-amount, 255));
}

int sg_luma(uint32_t c)
{
    return (int)((((c >> 16) & 255) * 299 + ((c >> 8) & 255) * 587 + (c & 255) * 114) / 1000);
}

static inline void blend(struct sg_canvas *c, int x, int y, uint32_t color, uint32_t a)
{
    if (x < c->cx0 || y < c->cy0 || x >= c->cx1 || y >= c->cy1 || a == 0)
        return;
    uint32_t *p = &c->px[y * c->w + x];
    *p = a >= 255 ? (color & 0xffffff) : sg_mix(*p, color, a);
}

void sg_fill(struct sg_canvas *c, int x, int y, int w, int h, uint32_t color)
{
    int x0 = imax(x, c->cx0), y0 = imax(y, c->cy0);
    int x1 = imin(x + w, c->cx1), y1 = imin(y + h, c->cy1);
    color &= 0xffffff;
    for (int yy = y0; yy < y1; yy++) {
        uint32_t *row = &c->px[yy * c->w];
        for (int xx = x0; xx < x1; xx++)
            row[xx] = color;
    }
}

void sg_fill_a(struct sg_canvas *c, int x, int y, int w, int h, uint32_t color, uint32_t alpha)
{
    if (alpha >= 255) {
        sg_fill(c, x, y, w, h, color);
        return;
    }
    int x0 = imax(x, c->cx0), y0 = imax(y, c->cy0);
    int x1 = imin(x + w, c->cx1), y1 = imin(y + h, c->cy1);
    for (int yy = y0; yy < y1; yy++) {
        uint32_t *row = &c->px[yy * c->w];
        for (int xx = x0; xx < x1; xx++)
            row[xx] = sg_mix(row[xx], color, alpha);
    }
}

void sg_vgrad(struct sg_canvas *c, int x, int y, int w, int h, uint32_t top, uint32_t bottom)
{
    for (int i = 0; i < h; i++)
        sg_fill(c, x, y + i, w, 1, sg_mix(top, bottom, h > 1 ? (uint32_t)(i * 255 / (h - 1)) : 0));
}

void sg_hgrad(struct sg_canvas *c, int x, int y, int w, int h, uint32_t left, uint32_t right)
{
    for (int i = 0; i < w; i++)
        sg_fill(c, x + i, y, 1, h, sg_mix(left, right, w > 1 ? (uint32_t)(i * 255 / (w - 1)) : 0));
}

static float sg_sqrt(float v) { return __builtin_sqrtf(v); }

static uint32_t corner_cov(int px, int py, int x, int y, int w, int h, int r)
{
    float fx = (float)px + 0.5f, fy = (float)py + 0.5f;
    float cx, cy;
    if (fx < x + r)
        cx = (float)(x + r);
    else if (fx > x + w - r)
        cx = (float)(x + w - r);
    else
        return 255;
    if (fy < y + r)
        cy = (float)(y + r);
    else if (fy > y + h - r)
        cy = (float)(y + h - r);
    else
        return 255;
    float dx = fx - cx, dy = fy - cy;
    float d = sg_sqrt(dx * dx + dy * dy);
    float cov = (float)r + 0.5f - d;
    if (cov <= 0.0f)
        return 0;
    if (cov >= 1.0f)
        return 255;
    return (uint32_t)(cov * 255.0f);
}

static void rrect_impl(struct sg_canvas *c, int x, int y, int w, int h, int r, uint32_t c0,
                       uint32_t c1, int grad, uint32_t alpha)
{
    if (w <= 0 || h <= 0)
        return;
    r = imin(r, imin(w, h) / 2);
    int x0 = imax(x, c->cx0), y0 = imax(y, c->cy0);
    int x1 = imin(x + w, c->cx1), y1 = imin(y + h, c->cy1);
    for (int yy = y0; yy < y1; yy++) {
        int in_corner_rows = yy < y + r || yy >= y + h - r;
        uint32_t *row = &c->px[yy * c->w];
        for (int xx = x0; xx < x1; xx++) {
            uint32_t cov = 255;
            if (in_corner_rows && (xx < x + r || xx >= x + w - r))
                cov = corner_cov(xx, yy, x, y, w, h, r);
            if (!cov)
                continue;
            uint32_t col = c0;
            if (grad == 1)
                col = sg_mix(c0, c1, h > 1 ? (uint32_t)((yy - y) * 255 / (h - 1)) : 0);
            else if (grad == 2)
                col = sg_mix(c0, c1, w > 1 ? (uint32_t)((xx - x) * 255 / (w - 1)) : 0);
            uint32_t a = cov * alpha / 255;
            row[xx] = a >= 255 ? (col & 0xffffff) : sg_mix(row[xx], col, a);
        }
    }
}

void sg_rrect(struct sg_canvas *c, int x, int y, int w, int h, int r, uint32_t color, uint32_t alpha)
{
    rrect_impl(c, x, y, w, h, r, color, color, 0, alpha);
}

void sg_rrect_grad(struct sg_canvas *c, int x, int y, int w, int h, int r, uint32_t c0, uint32_t c1,
                   bool horizontal)
{
    rrect_impl(c, x, y, w, h, r, c0, c1, horizontal ? 2 : 1, 255);
}

void sg_rrect_stroke(struct sg_canvas *c, int x, int y, int w, int h, int r, uint32_t color,
                     uint32_t alpha)
{
    if (w <= 2 || h <= 2)
        return;
    r = imin(r, imin(w, h) / 2);
    int x0 = imax(x, c->cx0), y0 = imax(y, c->cy0);
    int x1 = imin(x + w, c->cx1), y1 = imin(y + h, c->cy1);
    for (int yy = y0; yy < y1; yy++) {
        uint32_t *row = &c->px[yy * c->w];
        int edge_row = yy == y || yy == y + h - 1;
        int corner_rows = yy < y + r || yy >= y + h - r;
        for (int xx = x0; xx < x1; xx++) {
            uint32_t cov;
            if (corner_rows && (xx < x + r || xx >= x + w - r)) {
                uint32_t outer = corner_cov(xx, yy, x, y, w, h, r);
                uint32_t inner = corner_cov(xx, yy, x + 1, y + 1, w - 2, h - 2, imax(r - 1, 0));
                cov = outer > inner ? outer - inner : 0;
            } else if (edge_row || xx == x || xx == x + w - 1) {
                cov = 255;
            } else {
                xx = x + w - 2 < x1 ? x + w - 2 : x1 - 1;
                continue;
            }
            uint32_t a = cov * alpha / 255;
            if (a)
                row[xx] = a >= 255 ? (color & 0xffffff) : sg_mix(row[xx], color, a);
        }
    }
}

void sg_shadow(struct sg_canvas *c, int x, int y, int w, int h, int r, int spread, uint32_t alpha)
{
    for (int i = spread; i > 0; i--) {
        uint32_t a = alpha * (uint32_t)(spread - i + 1) / (uint32_t)(spread * spread);
        rrect_impl(c, x - i, y - i + spread / 2, w + 2 * i, h + 2 * i, r + i, 0, 0, 0, a);
    }
}

void sg_circle(struct sg_canvas *c, int cx, int cy, int r, uint32_t color, uint32_t alpha)
{
    rrect_impl(c, cx - r, cy - r, 2 * r, 2 * r, r, color, color, 0, alpha);
}

static float sg_atan2(float y, float x)
{
    const float pi = 3.14159265f;
    if (x == 0.0f && y == 0.0f)
        return 0.0f;
    float ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
    float a = ax > ay ? ay / ax : ax / ay;
    float s = a * a;
    float r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;
    if (ay > ax)
        r = pi / 2 - r;
    if (x < 0)
        r = pi - r;
    if (y < 0)
        r = -r;
    return r;
}

void sg_ring(struct sg_canvas *c, int cx, int cy, int r, int thick, uint32_t permille,
             uint32_t color, uint32_t track)
{
    const float pi = 3.14159265f;
    float limit = (float)(permille > 1000 ? 1000 : permille) / 1000.0f * 2.0f * pi;
    float ro = (float)r, ri = (float)(r - thick);
    int x0 = imax(cx - r - 1, c->cx0), x1 = imin(cx + r + 1, c->cx1);
    int y0 = imax(cy - r - 1, c->cy0), y1 = imin(cy + r + 1, c->cy1);
    for (int yy = y0; yy < y1; yy++) {
        for (int xx = x0; xx < x1; xx++) {
            float dx = (float)xx + 0.5f - (float)cx, dy = (float)yy + 0.5f - (float)cy;
            float d = sg_sqrt(dx * dx + dy * dy);
            float co = ro + 0.5f - d, ci = d - ri + 0.5f;
            if (co <= 0 || ci <= 0)
                continue;
            float cov = co < 1 ? co : 1;
            if (ci < 1)
                cov *= ci;
            float ang = sg_atan2(dx, -dy);
            if (ang < 0)
                ang += 2 * pi;
            uint32_t col = ang <= limit ? color : track;
            blend(c, xx, yy, col, (uint32_t)(cov * 255.0f));
        }
    }
}

int sg_font_h(int font) { return sf_fonts[font].height; }

int sg_text_w(int font, const char *s)
{
    const struct sf_font *f = &sf_fonts[font];
    int w = 0;
    for (; s && *s; s++) {
        unsigned ch = (unsigned char)*s;
        if (ch < 32 || ch > 126)
            ch = '?';
        w += f->glyphs[ch - 32].adv;
    }
    return w;
}

int sg_text(struct sg_canvas *c, int font, int x, int y, const char *s, uint32_t color)
{
    const struct sf_font *f = &sf_fonts[font];
    int pen = x;
    for (; s && *s; s++) {
        unsigned ch = (unsigned char)*s;
        if (ch < 32 || ch > 126)
            ch = '?';
        const struct sf_glyph *g = &f->glyphs[ch - 32];
        const uint8_t *bits = f->bits + g->off;
        int gx = pen + g->x, gy = y + g->y;
        if (gx < c->cx1 && gy < c->cy1 && gx + g->w > c->cx0 && gy + g->h > c->cy0) {
            for (int j = 0; j < g->h; j++)
                for (int i = 0; i < g->w; i++) {
                    uint32_t a = bits[j * g->w + i];
                    if (a)
                        blend(c, gx + i, gy + j, color, a);
                }
        }
        pen += g->adv;
    }
    return pen - x;
}

int sg_text_fit(struct sg_canvas *c, int font, int x, int y, int maxw, const char *s, uint32_t color)
{
    if (sg_text_w(font, s) <= maxw)
        return sg_text(c, font, x, y, s, color);
    char buf[160];
    int n = 0;
    int dots = sg_text_w(font, "...");
    int w = 0;
    const struct sf_font *f = &sf_fonts[font];
    while (s[n] && n < (int)sizeof(buf) - 4) {
        unsigned ch = (unsigned char)s[n];
        if (ch < 32 || ch > 126)
            ch = '?';
        int a = f->glyphs[ch - 32].adv;
        if (w + a + dots > maxw)
            break;
        buf[n] = s[n];
        w += a;
        n++;
    }
    buf[n++] = '.';
    buf[n++] = '.';
    buf[n++] = '.';
    buf[n] = 0;
    return sg_text(c, font, x, y, buf, color);
}

void sg_text_center(struct sg_canvas *c, int font, int x, int y, int w, const char *s, uint32_t color)
{
    sg_text(c, font, x + (w - sg_text_w(font, s)) / 2, y, s, color);
}

int sg_icon_px(int size) { return sf_icon_size[size]; }

void sg_icon(struct sg_canvas *c, int icon, int size, int x, int y, uint32_t color)
{
    if (icon < 0 || icon >= SI_COUNT || size < 0 || size >= SF_ICON_SIZES)
        return;
    int n = sf_icon_size[size];
    const uint8_t *bits = sf_icon_bits + sf_icon_off[size][icon];
    for (int j = 0; j < n; j++)
        for (int i = 0; i < n; i++) {
            uint32_t a = bits[j * n + i];
            if (a)
                blend(c, x + i, y + j, color, a);
        }
}

static void bitfont_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color, void *ctx)
{
    sg_fill((struct sg_canvas *)ctx, (int)x, (int)y, (int)w, (int)h, color);
}

void sg_bitfont(struct sg_canvas *c, int face, int x, int y, const char *s, uint32_t color, int size)
{
    if (x < 0 || y < 0)
        return;
    gfx_draw_text(s, (uint32_t)x, (uint32_t)y, color, (uint32_t)size, (gfx_font_face_t)face,
                  bitfont_rect, c);
}
