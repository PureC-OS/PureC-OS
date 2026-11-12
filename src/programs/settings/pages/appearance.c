#include "settings/app.h"
#include "settings/fmt.h"
#include "../../../libc/include/purec.h"
#include "../../../libfs/include/purefs.h"

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

static void theme_preview(struct ui *u, int x, int y, int w, int h, const struct pg_theme *t)
{
    sg_rrect(u->c, x, y, w, h, 8, t->desktop, 255);
    int wx = x + 12, wy = y + 12, ww = w - 24, wh = h - 22;
    sg_rrect(u->c, wx, wy, ww, wh, 6, t->window, 255);
    sg_rrect(u->c, wx, wy, ww, 14, 6, t->titlebar, 255);
    sg_fill(u->c, wx, wy + 8, ww, 6, t->titlebar);
    sg_circle(u->c, wx + ww - 9, wy + 7, 3, t->danger, 255);
    sg_circle(u->c, wx + ww - 19, wy + 7, 3, t->accent, 255);
    sg_rrect(u->c, wx + 8, wy + 22, ww * 6 / 10, 5, 2, t->text, 255);
    sg_rrect(u->c, wx + 8, wy + 32, ww * 4 / 10, 4, 2, t->muted_text, 255);
    sg_rrect(u->c, wx + 8, wy + 40, ww * 5 / 10, 4, 2, t->muted_text, 255);
    sg_rrect(u->c, wx + ww - 44, wy + wh - 18, 36, 12, 4, t->accent, 255);
}

int page_appearance(struct page_ctx *p)
{
    struct settings_app *app = p->app;
    struct ui *u = p->u;
    const struct ui_palette *pal = &u->pal;
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
    ui_card(u, p->x, y, p->w, ch);
    ui_card_title(u, p->x + 20, y + 20, SI_THEME_LIGHT_DARK, "Color theme", "Windows, desktop and this app");
    for (int i = 0; i < count; i++) {
        const char *name = pg_theme_name_at((uint32_t)i);
        struct pg_theme t = pg_theme_by_name(name);
        int cx = p->x + 24 + (i % cols) * (tw + gap);
        int cy = y + 84 + (i / cols) * (th + 34 + gap);
        bool sel = fmt_streq(a->theme, name);
        uint32_t id = ui_id("theme", i);
        bool clicked = ui_click(u, id, cx, cy, tw, th + 30);
        bool hot = u->hot == id;
        if (sel || hot)
            sg_rrect(u->c, cx - 4, cy - 4, tw + 8, th + 8, 12, sel ? pal->accent : pal->surface3, 255);
        theme_preview(u, cx, cy, tw, th, &t);
        sg_text_fit(u->c, sel ? SF_MEDIUM : SF_BODY, cx + 2, cy + th + 10, tw - 24,
                    pretty_theme(name, buf, sizeof(buf)), sel ? pal->text : pal->text2);
        if (sel)
            sg_icon(u->c, SI_CHECK_CIRCLE, SG_ICON_S, cx + tw - 18, cy + th + 8, pal->accent);
        if (clicked && !sel) {
            fmt_copy(a->theme, name, sizeof(a->theme));
            commit(app, "Theme applied");
        }
    }
    y += ch + 16;

    int wcount = (int)appearance_wallpaper_count();
    int row_h = 54;
    int wh = 84 + wcount * row_h + 10;
    ui_card(u, p->x, y, p->w, wh);
    ui_card_title(u, p->x + 20, y + 20, SI_WALLPAPER, "Wallpaper",
                  "BMP or PNG, stretched. Add your own as /config/wp.png");
    int yy = y + 80;
    for (int i = 0; i < wcount; i++) {
        const char *path = appearance_wallpaper_at((uint32_t)i);
        bool sel = fmt_streq(a->wallpaper, path);
        if (ui_row(u, ui_id("wp", i), p->x + 10, yy, p->w - 20, row_h - 6, sel) && !sel) {
            fmt_copy(a->wallpaper, path, sizeof(a->wallpaper));
            commit(app, "Wallpaper changed");
        }
        int tx = p->x + 26;
        if (path[0]) {
            sg_rrect_grad(u->c, tx, yy + 7, 56, 34, 6, sg_mix(pal->accent, 0x0f172a, 140),
                          sg_mix(pal->info, 0x0f172a, 120), true);
            sg_icon(u->c, SI_WALLPAPER, SG_ICON_S, tx + 19, yy + 15, 0xffffff);
        } else {
            struct pg_theme t = pg_theme_by_name(a->theme);
            sg_rrect(u->c, tx, yy + 7, 56, 34, 6, t.desktop, 255);
            sg_rrect_stroke(u->c, tx, yy + 7, 56, 34, 6, pal->border, 200);
        }
        sg_text_fit(u->c, SF_MEDIUM, tx + 72, yy + 7, p->w - 200, path[0] ? basename_of(path) : "Solid color",
                    pal->text);
        sg_text_fit(u->c, SF_SMALL, tx + 72, yy + 27, p->w - 200, path[0] ? path : "Uses the theme desktop color",
                    pal->text3);
        ui_radio(u, p->x + p->w - 48, yy + 15, sel);
        yy += row_h;
    }
    y += wh + 16;

    int fh = 296;
    ui_card(u, p->x, y, p->w, fh);
    ui_card_title(u, p->x + 20, y + 20, SI_FORMAT_FONT, "System font", "Used by the desktop and classic apps");
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
    if (ui_segmented(u, ui_id("font", 0), p->x + 24, y + 84, p->w - 48, 42, labels, fcount, &fsel)) {
        fmt_copy(a->font, appearance_font_name_at((uint32_t)fsel), sizeof(a->font));
        commit(app, "Font changed");
    }
    const char *fn = appearance_font_name_at((uint32_t)fsel);
    int face = fmt_streq(fn, "classic") ? 0 : fmt_streq(fn, "bold") ? 2 : 1;
    int py = y + 142;
    sg_rrect(u->c, p->x + 24, py, p->w - 48, 74, 10, pal->light ? 0xffffff : pal->bg, 255);
    sg_rrect_stroke(u->c, p->x + 24, py, p->w - 48, 74, 10, pal->border, 140);
    struct sg_clip saved = sg_clip_push(u->c, p->x + 30, py, p->w - 60, 74);
    int sz = (int)a->font_size;
    sg_bitfont(u->c, face, p->x + 40, py + 37 - sz / 2, "The quick brown fox jumps 0123", pal->text, sz);
    sg_clip_pop(u->c, saved);
    sg_text(u->c, SF_MEDIUM, p->x + 24, py + 94, "Size", pal->text);
    fmt_init(&f, buf, sizeof(buf));
    fu(&f, a->font_size);
    fs(&f, " px");
    sg_text(u->c, SF_MEDIUM, p->x + p->w - 24 - sg_text_w(SF_MEDIUM, buf), py + 94, buf, pal->accent);
    int v = (int)a->font_size;
    int r = ui_slider(u, ui_id("fsize", 0), p->x + 80, py + 90, p->w - 80 - 90, &v, APPEAR_MIN_FONT_SIZE,
                      APPEAR_MAX_FONT_SIZE, pal->accent);
    if (r) {
        a->font_size = (uint32_t)v;
        if (r == 2)
            commit(app, "Font size saved");
    }
    y += fh;
    return y - p->y;
}
