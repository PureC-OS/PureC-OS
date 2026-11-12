#include "settings/app.h"
#include "settings/fmt.h"
#include "../../../libc/include/purec.h"
#include "../../../libfs/include/purefs.h"
#include "settings/thumb.h"

#define THUMB_MAX 24
#define THUMB_W 220
#define THUMB_H 124

static struct thumb {
    int state;
    int w, h;
    uint32_t px[THUMB_W * THUMB_H];
} thumbs[THUMB_MAX];

static void commit(struct settings_app *app, const char *what)
{
    int32_t rc = appearance_save(&app->appearance);
    if (rc >= 0) {
        settings_toast(app, what, 0xa6e3a1);
        pc_desktop_redraw();
    } else {
        settings_toast(app, "Could not save /config/appear.ini (read-only disk?)", 0xf38ba8);
    }
}

static const char *pretty_theme(const char *name, char *buf, int cap)
{
    int i = 0;
    bool up = true;
    for (; name[i] && i < cap - 1; i++) {
        char ch = name[i];
        if (ch == '-') {
            buf[i] = ' ';
            up = true;
            continue;
        }
        buf[i] = up && ch >= 'a' && ch <= 'z' ? (char)(ch - 32) : ch;
        up = false;
    }
    buf[i] = 0;
    return buf;
}

static const char *basename_of(const char *path)
{
    const char *b = path;
    for (const char *s = path; *s; s++)
        if (*s == '/')
            b = s + 1;
    return b;
}

static void theme_preview(struct pg_ui *u, int x, int y, int w, int h, const struct pg_theme *t)
{
    pgc_rrect(u->c, x, y, w, h, 8, t->desktop, 255);
    int wx = x + 12, wy = y + 12, ww = w - 24, wh = h - 22;
    pgc_rrect(u->c, wx, wy, ww, wh, 6, t->window, 255);
    pgc_rrect(u->c, wx, wy, ww, 14, 6, t->titlebar, 255);
    pgc_fill(u->c, wx, wy + 8, ww, 6, t->titlebar);
    pgc_circle(u->c, wx + ww - 9, wy + 7, 3, t->danger, 255);
    pgc_circle(u->c, wx + ww - 19, wy + 7, 3, t->accent, 255);
    pgc_rrect(u->c, wx + 8, wy + 22, ww * 6 / 10, 5, 2, t->text, 255);
    pgc_rrect(u->c, wx + 8, wy + 32, ww * 4 / 10, 4, 2, t->muted_text, 255);
    pgc_rrect(u->c, wx + 8, wy + 40, ww * 5 / 10, 4, 2, t->muted_text, 255);
    pgc_rrect(u->c, wx + ww - 44, wy + wh - 18, 36, 12, 4, t->accent, 255);
}

int page_appearance(struct page_ctx *p)
{
    struct settings_app *app = p->app;
    struct pg_ui *u = p->u;
    const struct pg_palette *pal = &u->pal;
    struct personalization_settings *a = &app->appearance;
    int y = p->y;
    struct fmt f;
    char buf[96];

    int count = (int)pg_theme_count();
    int cols = p->w > 700 ? 5 : 3;
    int gap = 14;
    int tw = (p->w - 48 - gap * (cols - 1)) / cols;
    int th = tw * 66 / 100;
    int rows = (count + cols - 1) / cols;
    int ch = 84 + rows * (th + 34 + gap) + 6;
    pgu_card(u, p->x, y, p->w, ch);
    pgu_card_title(u, p->x + 20, y + 20, PGI_THEME_LIGHT_DARK, "Color theme", "Windows, desktop and this app");
    for (int i = 0; i < count; i++) {
        const char *name = pg_theme_name_at((uint32_t)i);
        struct pg_theme t = pg_theme_by_name(name);
        int cx = p->x + 24 + (i % cols) * (tw + gap);
        int cy = y + 84 + (i / cols) * (th + 34 + gap);
        bool sel = fmt_streq(a->theme, name);
        uint32_t id = pgu_id("theme", i);
        bool clicked = pgu_click(u, id, cx, cy, tw, th + 30);
        bool hot = u->hot == id;
        if (sel || hot)
            pgc_rrect(u->c, cx - 4, cy - 4, tw + 8, th + 8, 12, sel ? pal->accent : pal->surface3, 255);
        theme_preview(u, cx, cy, tw, th, &t);
        pgc_text_fit(u->c, sel ? PGF_MEDIUM : PGF_BODY, cx + 2, cy + th + 10, tw - 24,
                    pretty_theme(name, buf, sizeof(buf)), sel ? pal->text : pal->text2);
        if (sel)
            pgc_icon(u->c, PGI_CHECK_CIRCLE, PG_ICON_S, cx + tw - 18, cy + th + 8, pal->accent);
        if (clicked && !sel) {
            fmt_copy(a->theme, name, sizeof(a->theme));
            commit(app, "Theme applied");
        }
    }
    y += ch + 16;

    int wcount = (int)appearance_wallpaper_count();
    if (wcount > THUMB_MAX)
        wcount = THUMB_MAX;
    int wcols = p->w > 700 ? 4 : 3;
    int wgap = 14;
    int ww = (p->w - 48 - wgap * (wcols - 1)) / wcols;
    int wth = ww * 9 / 16;
    int wrows = (wcount + wcols - 1) / wcols;
    int wh = 84 + wrows * (wth + 40 + wgap) + 4;
    pgu_card(u, p->x, y, p->w, wh);
    pgu_card_title(u, p->x + 20, y + 20, PGI_WALLPAPER, "Wallpaper",
                  "PNG or BMP, stretched to the screen. Add your own as /config/wp.png");
    bool decoded_one = false;
    for (int i = 0; i < wcount; i++) {
        const char *path = appearance_wallpaper_at((uint32_t)i);
        int cx = p->x + 24 + (i % wcols) * (ww + wgap);
        int cy = y + 84 + (i / wcols) * (wth + 40 + wgap);
        bool sel = fmt_streq(a->wallpaper, path);
        uint32_t id = pgu_id("wp", i);
        bool clicked = pgu_click(u, id, cx, cy, ww, wth + 32);
        bool hot = u->hot == id;
        if (sel || hot)
            pgc_rrect(u->c, cx - 4, cy - 4, ww + 8, wth + 8, 13, sel ? pal->accent : pal->surface3, 255);
        if (!path[0]) {
            struct pg_theme t = pg_theme_by_name(a->theme);
            pgc_rrect(u->c, cx, cy, ww, wth, 10, t.desktop, 255);
            pgc_icon(u->c, PGI_TEXTURE_BOX, PG_ICON_M, cx + (ww - 28) / 2, cy + (wth - 28) / 2,
                    pgc_mix(t.desktop, t.text, 120));
        } else {
            struct thumb *th = &thumbs[i];
            bool visible = cy + wth > u->c->cy0 && cy < u->c->cy1;
            if (th->state != 0 && (th->w != ww || th->h != wth))
                th->state = 0;
            if (th->state == 0 && visible && !decoded_one) {
                decoded_one = true;
                th->w = ww;
                th->h = wth;
                th->state = ww <= THUMB_W && wth <= THUMB_H &&
                                    thumb_decode_file(path, th->px, (uint32_t)ww, (uint32_t)wth)
                                ? 1
                                : 2;
            }
            if (th->state == 1) {
                pgc_image(u->c, cx, cy, ww, wth, th->px, 10);
            } else {
                pgc_rrect_grad(u->c, cx, cy, ww, wth, 10, pgc_mix(pal->accent, 0x0f172a, 150),
                              pgc_mix(pal->info, 0x0f172a, 140), true);
                pgc_icon(u->c, th->state == 2 ? PGI_ALERT_CIRCLE : PGI_WALLPAPER, PG_ICON_M, cx + (ww - 28) / 2,
                        cy + (wth - 28) / 2, 0xffffff);
                if (th->state == 0)
                    u->animating = true;
            }
        }
        pgc_text_fit(u->c, sel ? PGF_MEDIUM : PGF_BODY, cx + 2, cy + wth + 10, ww - 26,
                    path[0] ? basename_of(path) : "Solid color", sel ? pal->text : pal->text2);
        if (sel)
            pgc_icon(u->c, PGI_CHECK_CIRCLE, PG_ICON_S, cx + ww - 18, cy + wth + 8, pal->accent);
        if (clicked && !sel) {
            fmt_copy(a->wallpaper, path, sizeof(a->wallpaper));
            commit(app, "Wallpaper changed");
        }
    }
    y += wh + 16;

    int fh = 296;
    pgu_card(u, p->x, y, p->w, fh);
    pgu_card_title(u, p->x + 20, y + 20, PGI_FORMAT_FONT, "System font", "Used by the desktop and classic apps");
    int fcount = (int)appearance_font_count();
    if (fcount > 4)
        fcount = 4;
    const char *labels[4];
    char names[4][16];
    int fsel = 0;
    for (int i = 0; i < fcount; i++) {
        const char *n = appearance_font_name_at((uint32_t)i);
        pretty_theme(n, names[i], sizeof(names[i]));
        labels[i] = names[i];
        if (fmt_streq(a->font, n))
            fsel = i;
    }
    if (pgu_segmented(u, pgu_id("font", 0), p->x + 24, y + 84, p->w - 48, 42, labels, fcount, &fsel)) {
        fmt_copy(a->font, appearance_font_name_at((uint32_t)fsel), sizeof(a->font));
        commit(app, "Font changed");
    }
    const char *fn = appearance_font_name_at((uint32_t)fsel);
    int face = fmt_streq(fn, "classic") ? 0 : fmt_streq(fn, "bold") ? 2 : 1;
    int py = y + 142;
    pgc_rrect(u->c, p->x + 24, py, p->w - 48, 74, 10, pal->light ? 0xffffff : pal->bg, 255);
    pgc_rrect_stroke(u->c, p->x + 24, py, p->w - 48, 74, 10, pal->border, 140);
    struct pg_clip saved = pgc_clip_push(u->c, p->x + 30, py, p->w - 60, 74);
    int sz = (int)a->font_size;
    pgc_bitfont(u->c, face, p->x + 40, py + 37 - sz / 2, "The quick brown fox jumps 0123", pal->text, sz);
    pgc_clip_pop(u->c, saved);
    pgc_text(u->c, PGF_MEDIUM, p->x + 24, py + 94, "Size", pal->text);
    fmt_init(&f, buf, sizeof(buf));
    fu(&f, a->font_size);
    fs(&f, " px");
    pgc_text(u->c, PGF_MEDIUM, p->x + p->w - 24 - pgc_text_w(PGF_MEDIUM, buf), py + 94, buf, pal->accent);
    int v = (int)a->font_size;
    int r = pgu_slider(u, pgu_id("fsize", 0), p->x + 80, py + 90, p->w - 80 - 90, &v, APPEAR_MIN_FONT_SIZE,
                      APPEAR_MAX_FONT_SIZE, pal->accent);
    if (r) {
        a->font_size = (uint32_t)v;
        if (r == 2)
            commit(app, "Font size saved");
    }
    y += fh;
    return y - p->y;
}
