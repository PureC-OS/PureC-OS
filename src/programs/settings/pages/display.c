#include "settings/app.h"
#include "settings/fmt.h"
#include "../../../libc/include/purec.h"
#include "../../../libfs/include/purefs.h"

static uint32_t gcd(uint32_t a, uint32_t b)
{
    while (b) {
        uint32_t t = a % b;
        a = b;
        b = t;
    }
    return a ? a : 1;
}

static void aspect(struct fmt *f, uint32_t w, uint32_t h)
{
    uint32_t g = gcd(w, h);
    uint32_t aw = w / g, ah = h / g;
    if (aw > 21) {
        static const uint32_t known[][2] = {{16, 9}, {16, 10}, {4, 3}, {5, 4}, {21, 9}, {3, 2}};
        uint32_t best = 0, bestd = 0xffffffffu;
        for (uint32_t i = 0; i < sizeof(known) / sizeof(known[0]); i++) {
            uint32_t a1 = w * known[i][1], a2 = h * known[i][0];
            uint32_t dd = a1 > a2 ? a1 - a2 : a2 - a1;
            if (dd < bestd) {
                bestd = dd;
                best = i;
            }
        }
        fc(f, '~');
        aw = known[best][0];
        ah = known[best][1];
    }
    if (aw == 8 && ah == 5) {
        aw = 16;
        ah = 10;
    }
    fu(f, aw);
    fc(f, ':');
    fu(f, ah);
}

static void apply(struct settings_app *app)
{
    struct display_settings *d = &app->display;
    int32_t rc = display_apply_live(d);
    if (rc == 0) {
        struct pc_display_scale phys;
        struct pc_display_info info;
        if (pc_display_scale_info(&phys)) {
            d->width = phys.physical_width;
            d->height = phys.physical_height;
            d->scale = phys.scale_percent;
        }
        if (pc_display_get_info(&info) && info.available)
            d->bpp = info.bpp;
        if (display_save(d) < 0)
            settings_toast(app, "Applied, but could not save (read-only disk?)", 0xf9e2af);
        else
            settings_toast(app, "Display settings applied", 0xa6e3a1);
    } else if (rc == -2) {
        (void)display_load(d);
        settings_toast(app, "Mode switching is not supported on this machine", 0xf38ba8);
    } else {
        (void)display_load(d);
        settings_toast(app, "Could not apply display mode", 0xf38ba8);
    }
}

static void monitor(struct page_ctx *p, int x, int y, int w, int h, uint32_t rw, uint32_t rh)
{
    struct pg_ui *u = p->u;
    const struct pg_palette *pal = &u->pal;
    int sw = w, sh = (int)((uint64_t)w * rh / (rw ? rw : 1));
    if (sh > h - 30) {
        sh = h - 30;
        sw = (int)((uint64_t)sh * rw / (rh ? rh : 1));
    }
    int sx = x + (w - sw) / 2, sy = y;
    pgc_rrect(u->c, sx - 8, sy - 8, sw + 16, sh + 16, 12, pal->light ? 0x2b2b38 : 0x0b0b12, 255);
    pgc_rrect_grad(u->c, sx, sy, sw, sh, 5, pgc_mix(pal->accent, 0x1e1b4b, 150),
                  pgc_mix(pal->accent, 0x0f766e, 170), true);
    int ww = sw * 45 / 100, wh = sh * 45 / 100;
    pgc_rrect(u->c, sx + sw / 7, sy + sh / 5, ww, wh, 4, 0xffffff, 200);
    pgc_fill_a(u->c, sx + sw / 7, sy + sh / 5, ww, 6, pal->accent, 200);
    pgc_rrect(u->c, sx + sw / 2, sy + sh / 3, ww * 3 / 4, wh, 4, 0xffffff, 120);
    pgc_fill_a(u->c, sx, sy + sh - 8, sw, 8, 0x000000, 90);
    pgc_rrect(u->c, x + w / 2 - 34, sy + sh + 8, 68, 10, 3, pal->light ? 0x2b2b38 : 0x0b0b12, 255);
    pgc_rrect(u->c, x + w / 2 - 56, sy + sh + 16, 112, 6, 3, pal->light ? 0x2b2b38 : 0x0b0b12, 255);
}

int page_display(struct page_ctx *p)
{
    struct settings_app *app = p->app;
    struct pg_ui *u = p->u;
    const struct pg_palette *pal = &u->pal;
    struct display_settings *d = &app->display;
    int y = p->y;
    struct fmt f;

    struct pc_display_scale phys;
    struct pc_display_info info;
    bool have_phys = pc_display_scale_info(&phys);
    bool have_info = pc_display_get_info(&info) && info.available;
    uint32_t cur_w = have_phys ? phys.physical_width : d->width;
    uint32_t cur_h = have_phys ? phys.physical_height : d->height;

    int h = 230;
    pgu_card(u, p->x, y, p->w, h);
    int mw = p->w * 40 / 100;
    if (mw > 300)
        mw = 300;
    monitor(p, p->x + 30, y + 34, mw, h - 50, cur_w, cur_h);
    int tx = p->x + 60 + mw;
    char big[32];
    fmt_init(&f, big, sizeof(big));
    fu(&f, cur_w);
    fs(&f, " x ");
    fu(&f, cur_h);
    pgc_text(u->c, PGF_SMALL, tx, y + 40, "CURRENT MODE", pal->text3);
    pgc_text(u->c, PGF_HERO, tx, y + 58, big, pal->text);
    char sub[64];
    fmt_init(&f, sub, sizeof(sub));
    aspect(&f, cur_w, cur_h);
    fs(&f, "  -  ");
    fu(&f, have_info ? info.bpp : d->bpp);
    fs(&f, "-bit color  -  scale ");
    fu(&f, have_phys ? phys.scale_percent : 100);
    fc(&f, '%');
    pgc_text(u->c, PGF_BODY, tx, y + 104, sub, pal->text2);
    char eff[64];
    uint32_t sc = have_phys && phys.scale_percent ? phys.scale_percent : 100;
    fmt_init(&f, eff, sizeof(eff));
    fs(&f, "Apps see ");
    fu(&f, cur_w * 100 / sc);
    fs(&f, " x ");
    fu(&f, cur_h * 100 / sc);
    fs(&f, " logical pixels");
    pgc_text(u->c, PGF_SMALL, tx, y + 130, eff, pal->text3);
    int bx = tx;
    bx += pgu_badge(u, bx, y + 160, "Live switching", pal->success) + 8;
    pgu_badge(u, bx, y + 160, "No reboot needed", pal->info);
    y += h + 16;

    int count = (int)display_mode_count();
    int cols = p->w > 700 ? 4 : 3;
    int gap = 10;
    int cw = (p->w - 48 - gap * (cols - 1)) / cols;
    int ch = 62;
    int rows = (count + cols - 1) / cols;
    int rh = 84 + rows * (ch + gap) + 14;
    pgu_card(u, p->x, y, p->w, rh);
    pgu_card_title(u, p->x + 20, y + 20, PGI_MONITOR_SCREENSHOT, "Resolution",
                  "Works in QEMU and VirtualBox; real UEFI keeps the boot mode");
    int gy = y + 84;
    for (int i = 0; i < count; i++) {
        struct display_mode m = display_mode_at((uint32_t)i);
        int cx = p->x + 24 + (i % cols) * (cw + gap);
        int cy = gy + (i / cols) * (ch + gap);
        bool sel = m.width == cur_w && m.height == cur_h;
        uint32_t id = pgu_id("mode", i);
        bool clicked = pgu_click(u, id, cx, cy, cw, ch);
        bool hot = u->hot == id;
        pgc_rrect(u->c, cx, cy, cw, ch, 10, sel ? pgc_mix(pal->surface, pal->accent, 50)
                                               : hot ? pal->surface2 : pal->surface, 255);
        pgc_rrect_stroke(u->c, cx, cy, cw, ch, 10, sel ? pal->accent : pal->border, sel ? 255 : 150);
        char lbl[24];
        fmt_init(&f, lbl, sizeof(lbl));
        fu(&f, m.width);
        fs(&f, " x ");
        fu(&f, m.height);
        pgc_text(u->c, PGF_MEDIUM, cx + 14, cy + 12, lbl, sel ? pal->text : pal->text2);
        char asp[16];
        fmt_init(&f, asp, sizeof(asp));
        aspect(&f, m.width, m.height);
        pgc_text(u->c, PGF_SMALL, cx + 14, cy + 34, asp, pal->text3);
        if (sel)
            pgc_icon(u->c, PGI_CHECK_CIRCLE, PG_ICON_S, cx + cw - 30, cy + 22, pal->accent);
        if (clicked && !sel) {
            d->width = m.width;
            d->height = m.height;
            d->bpp = m.bpp;
            apply(app);
        }
    }
    y += rh + 16;

    int sh = 160;
    pgu_card(u, p->x, y, p->w, sh);
    pgu_card_title(u, p->x + 20, y + 20, PGI_BRIGHTNESS_6, "Interface scale",
                  "Makes text, icons and windows bigger");
    int n = (int)display_scale_count();
    if (n > 8)
        n = 8;
    static char labels[8][8];
    const char *ptrs[8];
    for (int i = 0; i < n; i++) {
        fmt_init(&f, labels[i], sizeof(labels[i]));
        fu(&f, display_scale_at((uint32_t)i));
        fc(&f, '%');
        ptrs[i] = labels[i];
    }
    int sel = (int)display_scale_index_of(have_phys ? phys.scale_percent : d->scale);
    if (pgu_segmented(u, pgu_id("scale", 0), p->x + 24, y + 86, p->w - 48, 44, ptrs, n, &sel)) {
        d->width = cur_w;
        d->height = cur_h;
        d->scale = display_scale_at((uint32_t)sel);
        apply(app);
    }
    y += sh;
    return y - p->y;
}
