#include "include/pg_canvas.h"
#include "../gfx/text.h"

static inline int imin(int a, int b) { return a < b ? a : b; }
static inline int imax(int a, int b) { return a > b ? a : b; }

void pgc_init_stride(struct pg_canvas *c, uint32_t *px, int w, int h, int stride)
{
    pgc_init(c, px, w, h);
    c->stride = stride;
}

void pgc_init(struct pg_canvas *c, uint32_t *px, int w, int h)
{
    c->px = px;
    c->w = w;
    c->stride = w;
    c->h = h;
    pgc_unclip(c);
}

void pgc_clip(struct pg_canvas *c, int x, int y, int w, int h)
{
    c->cx0 = imax(0, x);
    c->cy0 = imax(0, y);
    c->cx1 = imin(c->w, x + w);
    c->cy1 = imin(c->h, y + h);
}

void pgc_unclip(struct pg_canvas *c)
{
    c->cx0 = 0;
    c->cy0 = 0;
    c->cx1 = c->w;
    c->cy1 = c->h;
}

struct pg_clip pgc_clip_push(struct pg_canvas *c, int x, int y, int w, int h)
{
    struct pg_clip saved = {c->cx0, c->cy0, c->cx1, c->cy1};
    c->cx0 = imax(c->cx0, x);
    c->cy0 = imax(c->cy0, y);
    c->cx1 = imin(c->cx1, x + w);
    c->cy1 = imin(c->cy1, y + h);
    if (c->cx1 < c->cx0)
        c->cx1 = c->cx0;
    if (c->cy1 < c->cy0)
        c->cy1 = c->cy0;
    return saved;
}

void pgc_clip_pop(struct pg_canvas *c, struct pg_clip saved)
{
    c->cx0 = saved.x0;
    c->cy0 = saved.y0;
    c->cx1 = saved.x1;
    c->cy1 = saved.y1;
}

uint32_t pgc_mix(uint32_t a, uint32_t b, uint32_t t)
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

uint32_t pgc_lighten(uint32_t c, int amount)
{
    if (amount >= 0)
        return pgc_mix(c, 0xffffff, (uint32_t)imin(amount, 255));
    return pgc_mix(c, 0x000000, (uint32_t)imin(-amount, 255));
}

int pgc_luma(uint32_t c)
{
    return (int)((((c >> 16) & 255) * 299 + ((c >> 8) & 255) * 587 + (c & 255) * 114) / 1000);
}

static inline void blend(struct pg_canvas *c, int x, int y, uint32_t color, uint32_t a)
{
    if (x < c->cx0 || y < c->cy0 || x >= c->cx1 || y >= c->cy1 || a == 0)
        return;
    uint32_t *p = &c->px[y * c->stride + x];
    *p = (a >= 255 ? color : pgc_mix(*p, color, a)) | 0xFF000000u;
}

void pgc_fill(struct pg_canvas *c, int x, int y, int w, int h, uint32_t color)
{
    int x0 = imax(x, c->cx0), y0 = imax(y, c->cy0);
    int x1 = imin(x + w, c->cx1), y1 = imin(y + h, c->cy1);
    color |= 0xFF000000u;
    for (int yy = y0; yy < y1; yy++) {
        uint32_t *row = &c->px[yy * c->stride];
        for (int xx = x0; xx < x1; xx++)
            row[xx] = color;
    }
}

void pgc_fill_a(struct pg_canvas *c, int x, int y, int w, int h, uint32_t color, uint32_t alpha)
{
    if (alpha >= 255) {
        pgc_fill(c, x, y, w, h, color);
        return;
    }
    int x0 = imax(x, c->cx0), y0 = imax(y, c->cy0);
    int x1 = imin(x + w, c->cx1), y1 = imin(y + h, c->cy1);
    for (int yy = y0; yy < y1; yy++) {
        uint32_t *row = &c->px[yy * c->stride];
        for (int xx = x0; xx < x1; xx++)
            row[xx] = pgc_mix(row[xx], color, alpha) | 0xFF000000u;
    }
}

void pgc_vgrad(struct pg_canvas *c, int x, int y, int w, int h, uint32_t top, uint32_t bottom)
{
    for (int i = 0; i < h; i++)
        pgc_fill(c, x, y + i, w, 1, pgc_mix(top, bottom, h > 1 ? (uint32_t)(i * 255 / (h - 1)) : 0));
}

void pgc_hgrad(struct pg_canvas *c, int x, int y, int w, int h, uint32_t left, uint32_t right)
{
    for (int i = 0; i < w; i++)
        pgc_fill(c, x + i, y, 1, h, pgc_mix(left, right, w > 1 ? (uint32_t)(i * 255 / (w - 1)) : 0));
}

static float pgc_sqrt(float v) { return __builtin_sqrtf(v); }

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
    float d = pgc_sqrt(dx * dx + dy * dy);
    float cov = (float)r + 0.5f - d;
    if (cov <= 0.0f)
        return 0;
    if (cov >= 1.0f)
        return 255;
    return (uint32_t)(cov * 255.0f);
}

static void rrect_impl(struct pg_canvas *c, int x, int y, int w, int h, int r, uint32_t c0,
                       uint32_t c1, int grad, uint32_t alpha)
{
    if (w <= 0 || h <= 0)
        return;
    r = imin(r, imin(w, h) / 2);
    int x0 = imax(x, c->cx0), y0 = imax(y, c->cy0);
    int x1 = imin(x + w, c->cx1), y1 = imin(y + h, c->cy1);
    for (int yy = y0; yy < y1; yy++) {
        int in_corner_rows = yy < y + r || yy >= y + h - r;
        uint32_t *row = &c->px[yy * c->stride];
        for (int xx = x0; xx < x1; xx++) {
            uint32_t cov = 255;
            if (in_corner_rows && (xx < x + r || xx >= x + w - r))
                cov = corner_cov(xx, yy, x, y, w, h, r);
            if (!cov)
                continue;
            uint32_t col = c0;
            if (grad == 1)
                col = pgc_mix(c0, c1, h > 1 ? (uint32_t)((yy - y) * 255 / (h - 1)) : 0);
            else if (grad == 2)
                col = pgc_mix(c0, c1, w > 1 ? (uint32_t)((xx - x) * 255 / (w - 1)) : 0);
            uint32_t a = cov * alpha / 255;
            row[xx] = (a >= 255 ? col : pgc_mix(row[xx], col, a)) | 0xFF000000u;
        }
    }
}

void pgc_rrect(struct pg_canvas *c, int x, int y, int w, int h, int r, uint32_t color, uint32_t alpha)
{
    rrect_impl(c, x, y, w, h, r, color, color, 0, alpha);
}

void pgc_rrect_grad(struct pg_canvas *c, int x, int y, int w, int h, int r, uint32_t c0, uint32_t c1,
                   bool horizontal)
{
    rrect_impl(c, x, y, w, h, r, c0, c1, horizontal ? 2 : 1, 255);
}

void pgc_rrect_stroke(struct pg_canvas *c, int x, int y, int w, int h, int r, uint32_t color,
                     uint32_t alpha)
{
    if (w <= 2 || h <= 2)
        return;
    r = imin(r, imin(w, h) / 2);
    int x0 = imax(x, c->cx0), y0 = imax(y, c->cy0);
    int x1 = imin(x + w, c->cx1), y1 = imin(y + h, c->cy1);
    for (int yy = y0; yy < y1; yy++) {
        uint32_t *row = &c->px[yy * c->stride];
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
                row[xx] = (a >= 255 ? color : pgc_mix(row[xx], color, a)) | 0xFF000000u;
        }
    }
}

void pgc_shadow(struct pg_canvas *c, int x, int y, int w, int h, int r, int spread, uint32_t alpha)
{
    for (int i = spread; i > 0; i--) {
        uint32_t a = alpha * (uint32_t)(spread - i + 1) / (uint32_t)(spread * spread);
        rrect_impl(c, x - i, y - i + spread / 2, w + 2 * i, h + 2 * i, r + i, 0, 0, 0, a);
    }
}

void pgc_circle(struct pg_canvas *c, int cx, int cy, int r, uint32_t color, uint32_t alpha)
{
    rrect_impl(c, cx - r, cy - r, 2 * r, 2 * r, r, color, color, 0, alpha);
}

static float pgc_atan2(float y, float x)
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

void pgc_ring(struct pg_canvas *c, int cx, int cy, int r, int thick, uint32_t permille,
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
            float d = pgc_sqrt(dx * dx + dy * dy);
            float co = ro + 0.5f - d, ci = d - ri + 0.5f;
            if (co <= 0 || ci <= 0)
                continue;
            float cov = co < 1 ? co : 1;
            if (ci < 1)
                cov *= ci;
            float ang = pgc_atan2(dx, -dy);
            if (ang < 0)
                ang += 2 * pi;
            uint32_t col = ang <= limit ? color : track;
            blend(c, xx, yy, col, (uint32_t)(cov * 255.0f));
        }
    }
}

int pgc_font_h(int font) { return pgf_fonts[font].height; }

uint32_t pgc_utf8_next(const char **ps)
{
    const unsigned char *s = (const unsigned char *)*ps;
    uint32_t c = s[0];
    int n = 1;
    if (c >= 0xF0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80 && (s[3] & 0xC0) == 0x80) {
        c = ((c & 7) << 18) | ((s[1] & 63u) << 12) | ((s[2] & 63u) << 6) | (s[3] & 63u);
        n = 4;
    } else if (c >= 0xE0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
        c = ((c & 15) << 12) | ((s[1] & 63u) << 6) | (s[2] & 63u);
        n = 3;
    } else if (c >= 0xC0 && (s[1] & 0xC0) == 0x80) {
        c = ((c & 31) << 6) | (s[1] & 63u);
        n = 2;
    } else if (c >= 0x80) {
        c = '?';
    }
    *ps += n;
    return c;
}

const struct pgf_glyph *pgc_glyph(int font, uint32_t cp)
{
    const struct pgf_font *f = &pgf_fonts[font];
    if (cp >= 32 && cp <= 126)
        return &f->glyphs[cp - 32];
    int lo = 0, hi = (int)f->ext_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (f->ext_cp[mid] == cp)
            return &f->ext[mid];
        if (f->ext_cp[mid] < cp)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return 0;
}

static const struct pgf_glyph *glyph_or_q(int font, uint32_t cp, const uint8_t **bits)
{
    const struct pgf_glyph *g = pgc_glyph(font, cp);
    if (!g)
        g = &pgf_fonts[font].glyphs['?' - 32];
    *bits = pgf_fonts[font].bits + g->off;
    return g;
}

int pgc_glyph_draw(struct pg_canvas *c, int font, int x, int y, uint32_t cp, uint32_t color)
{
    const uint8_t *bits;
    const struct pgf_glyph *g = glyph_or_q(font, cp, &bits);
    int gx = x + g->x, gy = y + g->y;
    if (gx < c->cx1 && gy < c->cy1 && gx + g->w > c->cx0 && gy + g->h > c->cy0) {
        for (int j = 0; j < g->h; j++)
            for (int i = 0; i < g->w; i++) {
                uint32_t a = bits[j * g->w + i];
                if (a)
                    blend(c, gx + i, gy + j, color, a);
            }
    }
    return g->adv;
}

int pgc_text_w(int font, const char *s)
{
    int w = 0;
    while (s && *s) {
        const uint8_t *bits;
        w += glyph_or_q(font, pgc_utf8_next(&s), &bits)->adv;
    }
    return w;
}

int pgc_text(struct pg_canvas *c, int font, int x, int y, const char *s, uint32_t color)
{
    int pen = x;
    while (s && *s)
        pen += pgc_glyph_draw(c, font, pen, y, pgc_utf8_next(&s), color);
    return pen - x;
}

int pgc_text_fit(struct pg_canvas *c, int font, int x, int y, int maxw, const char *s, uint32_t color)
{
    if (pgc_text_w(font, s) <= maxw)
        return pgc_text(c, font, x, y, s, color);
    int dots = pgc_text_w(font, "...");
    int pen = x;
    while (s && *s) {
        const char *next = s;
        uint32_t cp = pgc_utf8_next(&next);
        const uint8_t *bits;
        int a = glyph_or_q(font, cp, &bits)->adv;
        if (pen - x + a + dots > maxw)
            break;
        pen += pgc_glyph_draw(c, font, pen, y, cp, color);
        s = next;
    }
    pen += pgc_text(c, font, pen, y, "...", color);
    return pen - x;
}

void pgc_text_center(struct pg_canvas *c, int font, int x, int y, int w, const char *s, uint32_t color)
{
    pgc_text(c, font, x + (w - pgc_text_w(font, s)) / 2, y, s, color);
}

int pgc_icon_px(int size) { return pgf_icon_size[size]; }

void pgc_icon(struct pg_canvas *c, int icon, int size, int x, int y, uint32_t color)
{
    if (icon < 0 || icon >= PGI_COUNT || size < 0 || size >= PGF_ICON_SIZES)
        return;
    int n = pgf_icon_size[size];
    const uint8_t *bits = pgf_icon_bits + pgf_icon_off[size][icon];
    for (int j = 0; j < n; j++)
        for (int i = 0; i < n; i++) {
            uint32_t a = bits[j * n + i];
            if (a)
                blend(c, x + i, y + j, color, a);
        }
}

static void bitfont_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color, void *ctx)
{
    pgc_fill((struct pg_canvas *)ctx, (int)x, (int)y, (int)w, (int)h, color);
}

void pgc_bitfont(struct pg_canvas *c, int face, int x, int y, const char *s, uint32_t color, int size)
{
    if (x < 0 || y < 0)
        return;
    gfx_draw_text(s, (uint32_t)x, (uint32_t)y, color, (uint32_t)size, (gfx_font_face_t)face,
                  bitfont_rect, c);
}

void pgc_image(struct pg_canvas *c, int x, int y, int w, int h, const uint32_t *px, int r)
{
    r = imin(r, imin(w, h) / 2);
    int x0 = imax(x, c->cx0), y0 = imax(y, c->cy0);
    int x1 = imin(x + w, c->cx1), y1 = imin(y + h, c->cy1);
    for (int yy = y0; yy < y1; yy++) {
        const uint32_t *src = px + (yy - y) * w;
        uint32_t *row = &c->px[yy * c->stride];
        bool corner_rows = yy < y + r || yy >= y + h - r;
        for (int xx = x0; xx < x1; xx++) {
            uint32_t cov = 255;
            if (corner_rows && (xx < x + r || xx >= x + w - r))
                cov = corner_cov(xx, yy, x, y, w, h, r);
            if (cov)
                row[xx] = (cov >= 255 ? src[xx - x] : pgc_mix(row[xx], src[xx - x], cov)) | 0xFF000000u;
        }
    }
}

float pgc_sin(float a)
{
    const float pi = 3.14159265f, tau = 6.2831853f;
    while (a > pi)
        a -= tau;
    while (a < -pi)
        a += tau;
    float s = a < 0 ? -1.0f : 1.0f;
    float x = a < 0 ? -a : a;
    if (x > pi / 2)
        x = pi - x;
    float x2 = x * x;
    float r = x * (1.0f - x2 / 6.0f * (1.0f - x2 / 20.0f * (1.0f - x2 / 42.0f * (1.0f - x2 / 72.0f))));
    return s * r;
}

float pgc_cos(float a) { return pgc_sin(a + 1.5707963f); }

void pgc_line(struct pg_canvas *c, float x0, float y0, float x1, float y1, float width, uint32_t color)
{
    float hw = width / 2.0f;
    int bx0 = (int)((x0 < x1 ? x0 : x1) - hw - 1), bx1 = (int)((x0 > x1 ? x0 : x1) + hw + 2);
    int by0 = (int)((y0 < y1 ? y0 : y1) - hw - 1), by1 = (int)((y0 > y1 ? y0 : y1) + hw + 2);
    float dx = x1 - x0, dy = y1 - y0;
    float len2 = dx * dx + dy * dy;
    for (int yy = imax(by0, c->cy0); yy < imin(by1, c->cy1); yy++) {
        for (int xx = imax(bx0, c->cx0); xx < imin(bx1, c->cx1); xx++) {
            float px = (float)xx + 0.5f - x0, py = (float)yy + 0.5f - y0;
            float t = len2 > 0 ? (px * dx + py * dy) / len2 : 0;
            if (t < 0)
                t = 0;
            if (t > 1)
                t = 1;
            float ex = px - t * dx, ey = py - t * dy;
            float d = pgc_sqrt(ex * ex + ey * ey);
            float cov = hw + 0.5f - d;
            if (cov <= 0)
                continue;
            blend(c, xx, yy, color, cov >= 1 ? 255 : (uint32_t)(cov * 255.0f));
        }
    }
}

int pgc_text_wrap(struct pg_canvas *c, int font, int x, int y, int maxw, const char *s, uint32_t color)
{
    int line_h = pgc_font_h(font) + 4;
    int cy = y;
    char line[512];
    while (s && *s) {
        const char *p = s, *last_space = 0;
        int w = 0;
        while (*p && *p != '\n') {
            const char *q = p;
            uint32_t cp = pgc_utf8_next(&q);
            const uint8_t *bits;
            int a = glyph_or_q(font, cp, &bits)->adv;
            if (w + a > maxw && p != s)
                break;
            if (cp == ' ')
                last_space = p;
            w += a;
            p = q;
        }
        const char *end = (*p && *p != '\n' && last_space) ? last_space : p;
        int n = (int)(end - s);
        if (n > (int)sizeof(line) - 1)
            n = (int)sizeof(line) - 1;
        for (int i = 0; i < n; i++)
            line[i] = s[i];
        line[n] = 0;
        if (c)
            pgc_text(c, font, x, cy, line, color);
        cy += line_h;
        s = end;
        while (*s == ' ')
            s++;
        if (*s == '\n')
            s++;
    }
    return cy - y;
}
