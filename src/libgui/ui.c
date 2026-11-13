#include "include/pg_ui.h"

uint32_t pgu_id(const char *s, int n)
{
    uint32_t h = 2166136261u;
    for (; s && *s; s++)
        h = (h ^ (uint8_t)*s) * 16777619u;
    h = (h ^ (uint32_t)n) * 16777619u;
    return h ? h : 1;
}

void pgu_palette_from_theme(struct pg_palette *p, const struct pg_theme *t)
{
    bool light = pgc_luma(t->window) > 140;
    p->light = light;
    p->bg = light ? pgc_lighten(t->window, -6) : pgc_lighten(t->window, -14);
    p->sidebar = light ? pgc_lighten(t->window, -14) : pgc_lighten(t->window, -26);
    p->surface = t->surface ? t->surface : pgc_mix(t->window, t->text, 14);
    p->surface2 = t->surface_alt ? pgc_mix(p->surface, t->surface_alt, 150) : pgc_mix(t->window, t->text, 24);
    p->surface3 = t->surface_alt ? t->surface_alt : pgc_mix(t->window, t->text, 38);
    p->border = light ? pgc_lighten(t->window, -34) : pgc_mix(p->surface3, t->window, 60);
    p->text = t->text;
    p->text2 = pgc_mix(t->text, light ? t->window : p->bg, 70);
    p->text3 = t->muted_text;
    p->accent = t->accent;
    p->accent2 = pgc_mix(t->accent, light ? 0x000000 : 0xffffff, 60);
    p->on_accent = t->on_accent ? t->on_accent : (pgc_luma(t->accent) > 150 ? 0x11111b : 0xffffff);
    p->danger = t->danger;
    p->success = t->success ? t->success : (light ? 0x2f9e44 : 0xa6e3a1);
    p->warning = t->warning ? t->warning : (light ? 0xe67700 : 0xf9e2af);
    p->info = t->info ? t->info : (light ? 0x1c7ed6 : 0x89dceb);
}

bool pgu_hover(struct pg_ui *u, int x, int y, int w, int h)
{
    return u->mx >= x && u->my >= y && u->mx < x + w && u->my < y + h;
}

static bool blocked(const struct pg_ui *u)
{
    return u->prev_block_w > 0 && u->mx >= u->prev_block_x && u->my >= u->prev_block_y &&
           u->mx < u->prev_block_x + u->prev_block_w && u->my < u->prev_block_y + u->prev_block_h;
}

static bool interact(struct pg_ui *u, uint32_t id, int x, int y, int w, int h)
{
    bool over = !u->modal && !blocked(u) && pgu_hover(u, x, y, w, h);
    if (over) {
        u->hot = id;
        u->any_hot = true;
    }
    if (over && u->pressed)
        u->active = id;
    bool clicked = false;
    if (u->released && u->active == id) {
        clicked = over;
        u->active = 0;
    }
    return clicked;
}

bool pgu_click(struct pg_ui *u, uint32_t id, int x, int y, int w, int h)
{
    return interact(u, id, x, y, w, h);
}

void pgu_card(struct pg_ui *u, int x, int y, int w, int h)
{
    const struct pg_palette *p = &u->pal;
    if (p->light)
        pgc_shadow(u->c, x, y, w, h, 12, 6, 50);
    pgc_rrect(u->c, x, y, w, h, 12, p->surface, 255);
    pgc_rrect_stroke(u->c, x, y, w, h, 12, p->border, p->light ? 160 : 120);
}

void pgu_icon_tile(struct pg_ui *u, int x, int y, int size, int icon, uint32_t color)
{
    int px = pgc_icon_px(PG_ICON_S);
    pgc_rrect(u->c, x, y, size, size, size / 3, color, u->pal.light ? 40 : 46);
    pgc_icon(u->c, icon, PG_ICON_S, x + (size - px) / 2, y + (size - px) / 2, color);
}

void pgu_card_title(struct pg_ui *u, int x, int y, int icon, const char *title, const char *subtitle)
{
    int tx = x;
    if (icon >= 0) {
        pgu_icon_tile(u, x, y, 34, icon, u->pal.accent);
        tx += 46;
    }
    if (subtitle) {
        pgc_text(u->c, PGF_HEAD, tx, y - 1, title, u->pal.text);
        pgc_text(u->c, PGF_SMALL, tx, y + 19, subtitle, u->pal.text3);
    } else {
        pgc_text(u->c, PGF_HEAD, tx, y + 7, title, u->pal.text);
    }
}

int pgu_button_w(const char *label, int icon)
{
    return pgc_text_w(PGF_MEDIUM, label) + 32 + (icon >= 0 ? 24 : 0);
}

bool pgu_button(struct pg_ui *u, uint32_t id, int x, int y, int w, int h, const char *label, int icon,
               enum pgu_btn_style style)
{
    const struct pg_palette *p = &u->pal;
    bool clicked = interact(u, id, x, y, w, h);
    bool hot = u->hot == id && !u->modal && pgu_hover(u, x, y, w, h);
    bool pressed = u->active == id && hot;
    uint32_t bg, fg;
    switch (style) {
    case PGU_BTN_PRIMARY:
        bg = p->accent;
        fg = p->on_accent;
        break;
    case PGU_BTN_DANGER:
        bg = p->danger;
        fg = pgc_luma(p->danger) > 150 ? 0x11111b : 0xffffff;
        break;
    case PGU_BTN_GHOST:
        bg = p->surface;
        fg = p->accent;
        break;
    default:
        bg = p->surface2;
        fg = p->text;
        break;
    }
    if (hot)
        bg = style == PGU_BTN_GHOST ? p->surface2 : pgc_lighten(bg, p->light ? -18 : 22);
    if (pressed)
        bg = pgc_lighten(bg, p->light ? -30 : -24);
    pgc_rrect(u->c, x, y, w, h, 8, bg, 255);
    if (style == PGU_BTN_SECONDARY)
        pgc_rrect_stroke(u->c, x, y, w, h, 8, p->border, 140);
    int tw = pgc_text_w(PGF_MEDIUM, label) + (icon >= 0 ? 24 : 0);
    int tx = x + (w - tw) / 2;
    int ty = y + (h - pgc_font_h(PGF_MEDIUM)) / 2;
    if (icon >= 0) {
        pgc_icon(u->c, icon, PG_ICON_S, tx, y + (h - 18) / 2, fg);
        tx += 24;
    }
    pgc_text(u->c, PGF_MEDIUM, tx, ty, label, fg);
    return clicked;
}

bool pgu_toggle(struct pg_ui *u, uint32_t id, int x, int y, bool *value)
{
    const struct pg_palette *p = &u->pal;
    const int w = 46, h = 26;
    bool clicked = interact(u, id, x, y, w, h);
    if (clicked)
        *value = !*value;
    bool hot = u->hot == id;
    uint32_t track = *value ? p->accent : p->surface3;
    if (hot)
        track = pgc_lighten(track, p->light ? -14 : 18);
    pgc_rrect(u->c, x, y, w, h, h / 2, track, 255);
    int kx = *value ? x + w - h + 3 : x + 3;
    pgc_circle(u->c, kx + (h - 6) / 2, y + h / 2, (h - 6) / 2, *value ? p->on_accent : 0xffffff, 255);
    return clicked;
}

int pgu_slider(struct pg_ui *u, uint32_t id, int x, int y, int w, int *value, int min, int max,
              uint32_t color)
{
    const struct pg_palette *p = &u->pal;
    const int h = 28, track_h = 6;
    int result = 0;
    bool was_active = u->active == id;
    interact(u, id, x - 6, y, w + 12, h);
    if (u->active == id && u->down) {
        int v = min + (int)((int64_t)(u->mx - x) * (max - min) / (w ? w : 1));
        if (v < min)
            v = min;
        if (v > max)
            v = max;
        if (v != *value) {
            *value = v;
            result = 1;
        }
    }
    if (u->released && was_active)
        result = 2;
    int span = max - min ? max - min : 1;
    int fill = (int)((int64_t)(*value - min) * w / span);
    int ty = y + (h - track_h) / 2;
    pgc_rrect(u->c, x, ty, w, track_h, track_h / 2, p->surface3, 255);
    if (fill > 0)
        pgc_rrect_grad(u->c, x, ty, fill > track_h ? fill : track_h, track_h, track_h / 2,
                      pgc_mix(color, p->surface, 60), color, true);
    bool hot = u->hot == id || u->active == id;
    int kr = hot ? 10 : 9;
    pgc_circle(u->c, x + fill, y + h / 2 + 1, kr + 1, 0x000000, 50);
    pgc_circle(u->c, x + fill, y + h / 2, kr, 0xffffff, 255);
    pgc_circle(u->c, x + fill, y + h / 2, kr - 5, color, 255);
    return result;
}

bool pgu_segmented(struct pg_ui *u, uint32_t id, int x, int y, int w, int h, const char *const *labels,
                  int count, int *selected)
{
    const struct pg_palette *p = &u->pal;
    bool changed = false;
    pgc_rrect(u->c, x, y, w, h, 9, p->surface2, 255);
    int seg = (w - 6) / (count ? count : 1);
    for (int i = 0; i < count; i++) {
        int sx = x + 3 + i * seg;
        uint32_t sid = id + (uint32_t)i * 7919u;
        if (interact(u, sid, sx, y + 3, seg, h - 6) && *selected != i) {
            *selected = i;
            changed = true;
        }
        bool sel = *selected == i;
        if (sel) {
            pgc_rrect(u->c, sx, y + 3, seg, h - 6, 7, p->accent, 255);
        } else if (u->hot == sid) {
            pgc_rrect(u->c, sx, y + 3, seg, h - 6, 7, p->surface3, 255);
        }
        pgc_text_center(u->c, PGF_MEDIUM, sx, y + (h - pgc_font_h(PGF_MEDIUM)) / 2, seg, labels[i],
                       sel ? p->on_accent : p->text2);
    }
    return changed;
}

bool pgu_row(struct pg_ui *u, uint32_t id, int x, int y, int w, int h, bool selected)
{
    const struct pg_palette *p = &u->pal;
    bool clicked = interact(u, id, x, y, w, h);
    if (selected)
        pgc_rrect(u->c, x, y, w, h, 10, p->accent, p->light ? 36 : 40);
    else if (u->hot == id)
        pgc_rrect(u->c, x, y, w, h, 10, p->surface2, 255);
    return clicked;
}

bool pgu_radio(struct pg_ui *u, int x, int y, bool on)
{
    const struct pg_palette *p = &u->pal;
    if (on) {
        pgc_circle(u->c, x + 9, y + 9, 9, p->accent, 255);
        pgc_circle(u->c, x + 9, y + 9, 4, p->on_accent, 255);
    } else {
        pgc_circle(u->c, x + 9, y + 9, 9, p->border, 255);
        pgc_circle(u->c, x + 9, y + 9, 7, p->surface, 255);
    }
    return on;
}

void pgu_check_mark(struct pg_ui *u, int x, int y, bool on)
{
    if (on)
        pgc_icon(u->c, PGI_CHECK_CIRCLE, PG_ICON_S, x, y, u->pal.accent);
}

bool pgu_textbox(struct pg_ui *u, uint32_t id, int x, int y, int w, int h, char *buf, int cap,
                bool password, const char *placeholder)
{
    const struct pg_palette *p = &u->pal;
    bool submitted = false;
    if (interact(u, id, x, y, w, h))
        u->focus = id;
    else if (u->pressed && !pgu_hover(u, x, y, w, h) && u->focus == id)
        u->focus = 0;
    bool focused = u->focus == id;
    int len = 0;
    while (buf[len])
        len++;
    if (focused && u->key) {
        if (u->key == '\b' || u->key == 127) {
            if (len > 0)
                buf[--len] = 0;
        } else if (u->key == '\n' || u->key == '\r') {
            submitted = true;
        } else if (u->key >= 32 && u->key < 127 && len < cap - 1) {
            buf[len++] = (char)u->key;
            buf[len] = 0;
        }
        u->key = 0;
    }
    pgc_rrect(u->c, x, y, w, h, 8, p->light ? 0xffffff : p->bg, 255);
    pgc_rrect_stroke(u->c, x, y, w, h, 8, focused ? p->accent : p->border, 255);
    if (focused)
        pgc_rrect_stroke(u->c, x - 2, y - 2, w + 4, h + 4, 10, p->accent, 70);
    int ty = y + (h - pgc_font_h(PGF_BODY)) / 2;
    char shown[96];
    int n = 0;
    for (; n < len && n < (int)sizeof(shown) - 1; n++)
        shown[n] = password ? '*' : buf[n];
    shown[n] = 0;
    struct pg_clip saved = pgc_clip_push(u->c, x + 10, y, w - 20, h);
    int tw = 0;
    if (len)
        tw = pgc_text(u->c, PGF_BODY, x + 12, ty, shown, p->text);
    else if (placeholder)
        pgc_text(u->c, PGF_BODY, x + 12, ty, placeholder, p->text3);
    if (focused && ((u->now / 500) & 1) == 0)
        pgc_fill(u->c, x + 13 + tw, y + 8, 2, h - 16, p->accent);
    pgc_clip_pop(u->c, saved);
    if (focused)
        u->animating = true;
    return submitted;
}

void pgu_progress(struct pg_ui *u, int x, int y, int w, int h, uint32_t permille, uint32_t color)
{
    if (permille > 1000)
        permille = 1000;
    pgc_rrect(u->c, x, y, w, h, h / 2, u->pal.surface3, 255);
    int fill = (int)((uint64_t)w * permille / 1000);
    if (fill > 0)
        pgc_rrect_grad(u->c, x, y, fill < h ? h : fill, h, h / 2, pgc_mix(color, u->pal.surface, 50),
                      color, true);
}

int pgu_badge(struct pg_ui *u, int x, int y, const char *text, uint32_t color)
{
    int w = pgc_text_w(PGF_SMALL, text) + 16;
    pgc_rrect(u->c, x, y, w, 22, 11, color, u->pal.light ? 40 : 46);
    pgc_text(u->c, PGF_SMALL, x + 8, y + 3, text, color);
    return w;
}

void pgu_kv(struct pg_ui *u, int x, int y, int w, const char *key, const char *value)
{
    pgc_text(u->c, PGF_BODY, x, y, key, u->pal.text3);
    int vw = pgc_text_w(PGF_MEDIUM, value);
    int kw = pgc_text_w(PGF_BODY, key) + 16;
    if (vw > w - kw)
        pgc_text_fit(u->c, PGF_MEDIUM, x + kw, y, w - kw, value, u->pal.text);
    else
        pgc_text(u->c, PGF_MEDIUM, x + w - vw, y, value, u->pal.text);
}

void pgu_separator(struct pg_ui *u, int x, int y, int w)
{
    pgc_fill_a(u->c, x, y, w, 1, u->pal.border, u->pal.light ? 200 : 140);
}

void pgu_signal_bars(struct pg_ui *u, int x, int y, int rssi, uint32_t color)
{
    int level = rssi >= -55 ? 4 : rssi >= -65 ? 3 : rssi >= -75 ? 2 : rssi >= -85 ? 1 : 0;
    static const int ids[5] = {PGI_WIFI_STRENGTH_OUTLINE, PGI_WIFI_STRENGTH_1, PGI_WIFI_STRENGTH_2,
                               PGI_WIFI_STRENGTH_3, PGI_WIFI_STRENGTH_4};
    pgc_icon(u->c, ids[level], PG_ICON_S, x, y, color);
}

void pgu_feed(struct pg_ui *u, const struct pg_window *w, const struct pg_event *ev)
{
    if (!u || !w || !ev)
        return;
    int mx = ev->x - (int32_t)w->client.x, my = ev->y - (int32_t)w->client.y;
    switch (ev->type) {
    case PG_EVENT_MOUSE_MOVE:
        u->mx = mx;
        u->my = my;
        break;
    case PG_EVENT_MOUSE_DOWN:
        u->mx = mx;
        u->my = my;
        if (ev->button == 1) {
            u->down = true;
            u->pressed = true;
        }
        break;
    case PG_EVENT_MOUSE_UP:
        u->mx = mx;
        u->my = my;
        if (ev->button == 1) {
            u->down = false;
            u->released = true;
        }
        break;
    case PG_EVENT_KEY:
        u->key = ev->key;
        break;
    case PG_EVENT_SPECIAL_KEY:
        u->special = ev->key;
        break;
    default:
        break;
    }
}

void pgu_frame_begin(struct pg_ui *u, struct pg_canvas *c, uint64_t now_ms)
{
    u->c = c;
    u->now = now_ms;
    u->hot = 0;
    u->any_hot = false;
    u->animating = false;
    u->prev_block_x = u->block_x;
    u->prev_block_y = u->block_y;
    u->prev_block_w = u->block_w;
    u->prev_block_h = u->block_h;
    u->block_w = 0;
    u->block_h = 0;
}

static void dropdown_overlay(struct pg_ui *u)
{
    const struct pg_palette *p = &u->pal;
    int row = 34;
    int h = u->dd_count * row + 8;
    int x = u->dd_x, y = u->dd_y;
    if (u->c && y + h > u->c->h)
        y = u->dd_y - h - 48;
    u->block_x = x;
    u->block_y = y;
    u->block_w = u->dd_w;
    u->block_h = h;
    pgc_shadow(u->c, x, y, u->dd_w, h, 10, 8, 90);
    pgc_rrect(u->c, x, y, u->dd_w, h, 10, p->light ? 0xffffff : p->surface, 255);
    pgc_rrect_stroke(u->c, x, y, u->dd_w, h, 10, p->border, 200);
    bool over_menu = u->mx >= x && u->my >= y && u->mx < x + u->dd_w && u->my < y + h;
    for (int i = 0; i < u->dd_count; i++) {
        int ry = y + 4 + i * row;
        bool hover = u->mx >= x && u->mx < x + u->dd_w && u->my >= ry && u->my < ry + row;
        if (hover)
            pgc_rrect(u->c, x + 4, ry, u->dd_w - 8, row, 7, p->accent, p->light ? 36 : 44);
        pgc_text_fit(u->c, i == u->dd_sel ? PGF_MEDIUM : PGF_BODY, x + 14, ry + (row - pgc_font_h(PGF_BODY)) / 2,
                     u->dd_w - 44, u->dd_labels[i], i == u->dd_sel ? p->text : p->text2);
        if (i == u->dd_sel)
            pgc_icon(u->c, PGI_CHECK, PG_ICON_S, x + u->dd_w - 30, ry + (row - 18) / 2, p->accent);
        if (hover && u->released) {
            u->dd_changed = u->dd_open;
            u->dd_result = i;
        }
    }
    if (u->released && !over_menu)
        u->dd_open = 0;
    else if (u->released && u->dd_changed)
        u->dd_open = 0;
    u->animating = true;
}

void pgu_frame_end(struct pg_ui *u)
{
    if (u->dd_open && u->dd_labels)
        dropdown_overlay(u);
    else
        u->dd_open = 0;
    u->pressed = false;
    u->released = false;
    u->key = 0;
    u->special = 0;
    u->wheel = 0;
}

bool pgu_checkbox(struct pg_ui *u, uint32_t id, int x, int y, const char *label, bool *value)
{
    const struct pg_palette *p = &u->pal;
    int tw = label ? pgc_text_w(PGF_BODY, label) + 10 : 0;
    bool clicked = interact(u, id, x, y, 22 + tw, 22);
    if (clicked)
        *value = !*value;
    bool hot = u->hot == id;
    if (*value) {
        pgc_rrect(u->c, x, y, 22, 22, 6, hot ? pgc_lighten(p->accent, p->light ? -14 : 18) : p->accent, 255);
        pgc_icon(u->c, PGI_CHECK, PG_ICON_S, x + 2, y + 2, p->on_accent);
    } else {
        pgc_rrect(u->c, x, y, 22, 22, 6, hot ? p->surface2 : (p->light ? 0xffffff : p->bg), 255);
        pgc_rrect_stroke(u->c, x, y, 22, 22, 6, hot ? p->accent : p->border, 255);
    }
    if (label)
        pgc_text(u->c, PGF_BODY, x + 32, y + (22 - pgc_font_h(PGF_BODY)) / 2, label, p->text);
    return clicked;
}

bool pgu_dropdown(struct pg_ui *u, uint32_t id, int x, int y, int w, int h, const char *const *labels,
                  int count, int *selected)
{
    const struct pg_palette *p = &u->pal;
    bool changed = false;
    if (u->dd_changed == id) {
        u->dd_changed = 0;
        if (u->dd_result >= 0 && u->dd_result < count && u->dd_result != *selected) {
            *selected = u->dd_result;
            changed = true;
        }
    }
    if (interact(u, id, x, y, w, h))
        u->dd_open = u->dd_open == id ? 0 : id;
    bool open = u->dd_open == id;
    bool hot = u->hot == id;
    pgc_rrect(u->c, x, y, w, h, 8, p->light ? 0xffffff : (hot ? p->surface2 : p->bg), 255);
    pgc_rrect_stroke(u->c, x, y, w, h, 8, open ? p->accent : p->border, 255);
    const char *cur = *selected >= 0 && *selected < count ? labels[*selected] : "";
    pgc_text_fit(u->c, PGF_BODY, x + 12, y + (h - pgc_font_h(PGF_BODY)) / 2, w - 44, cur, p->text);
    pgc_icon(u->c, PGI_CHEVRON_RIGHT, PG_ICON_S, x + w - 28, y + (h - 18) / 2, p->text3);
    if (open) {
        u->dd_x = x;
        u->dd_y = y + h + 4;
        u->dd_w = w;
        u->dd_count = count;
        u->dd_sel = *selected;
        u->dd_labels = labels;
    }
    return changed;
}

bool pgu_tabs(struct pg_ui *u, uint32_t id, int x, int y, int w, const char *const *labels, int count,
              int *selected)
{
    const struct pg_palette *p = &u->pal;
    bool changed = false;
    int tx = x;
    for (int i = 0; i < count; i++) {
        int tw = pgc_text_w(PGF_MEDIUM, labels[i]) + 28;
        uint32_t tid = id + (uint32_t)i * 104729u;
        if (interact(u, tid, tx, y, tw, 38) && *selected != i) {
            *selected = i;
            changed = true;
        }
        bool sel = *selected == i;
        if (!sel && u->hot == tid)
            pgc_rrect(u->c, tx, y + 2, tw, 32, 8, p->surface2, 255);
        pgc_text_center(u->c, PGF_MEDIUM, tx, y + 9, tw, labels[i], sel ? p->text : p->text3);
        if (sel)
            pgc_rrect(u->c, tx + 10, y + 35, tw - 20, 3, 1, p->accent, 255);
        tx += tw + 4;
    }
    pgc_fill_a(u->c, x, y + 38, w, 1, p->border, 140);
    return changed;
}
