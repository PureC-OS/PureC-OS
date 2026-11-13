#include "bottom_panel.h"
#include "desktop_entries.h"
#include "datetime_service.h"
#include "../display.h"
#include "../personalization.h"
#include "../window_manager.h"
#include "../syscall.h"
#include "../../kernel/syscall/syscall.h"
#include "../../lib/string.h"

#define START_BTN_W 78
#define QUICK_W 26
#define QUICK_COUNT 4
#define TASK_W 150
#define TASK_MIN_W 72
#define TASK_GAP 3
#define TRAY_W 116
#define MENU_W 236
#define MENU_ROW_H 24
#define MENU_BANNER_W 24
#define MENU_FOOTER_ROWS 2
#define MENU_SEPARATOR_H 8
#define MAX_TASKS 32
#define BTN_Y 3
#define BTN_H 24

static bool menu_open = false;
static enum bottom_panel_action pending_kind = BOTTOM_ACTION_NONE;
static char pending_path[128];
static char pending_builtin[16];
static uint8_t last_minute = 0xFF;
static int32_t menu_hover = -1;
static bool start_pressed_look = false;

static const char *quick_exec[4] = {
    "/bin/program/files",
    "/bin/program/terminal",
    "/bin/program/settings",
    "/bin/program/devmgr"
};
static const char *quick_tip[4] = { "Files", "Terminal", "Settings", "Devices" };

uint32_t bottom_panel_height(void) { return BOTTOM_PANEL_HEIGHT; }

void bottom_panel_init(void) {
    menu_open = false;
    pending_kind = BOTTOM_ACTION_NONE;
    pending_path[0] = '\0';
    pending_builtin[0] = '\0';
    last_minute = 0xFF;
    menu_hover = -1;
}

static bool point_in(int32_t x, int32_t y, uint32_t l, uint32_t t,
                     uint32_t w, uint32_t h) {
    return x >= (int32_t)l && y >= (int32_t)t &&
           x < (int32_t)(l + w) && y < (int32_t)(t + h);
}

static void copy_trunc(char *dst, uint32_t cap, const char *src) {
    if (!dst || !cap) return;
    uint32_t i = 0;
    while (src[i] && i + 1 < cap) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static void truncate_middle(const char *src, char *dst, uint32_t cap,
                            uint32_t max_chars) {
    uint32_t len = (uint32_t)strlen(src);
    if (len <= max_chars || cap == 0) { copy_trunc(dst, cap, src); return; }
    if (max_chars < 4 || cap < 5) { copy_trunc(dst, cap, src); return; }
    uint32_t keep = max_chars - 3;
    uint32_t i = 0;
    for (; i < keep && i + 1 < cap; i++) dst[i] = src[i];
    if (i + 3 < cap) { dst[i++] = '.'; dst[i++] = '.'; dst[i++] = '.'; }
    dst[i] = '\0';
}

static uint32_t menu_visible_count(void) {
    uint32_t visible = 0;
    uint32_t n = desktop_entries_count();
    for (uint32_t i = 0; i < n; i++) {
        const struct desktop_entry *e = desktop_entries_get(i);
        if (e && !e->hidden) visible++;
    }
    return visible;
}

static uint32_t menu_app_rows(void) {
    uint32_t n = menu_visible_count();
    return n > 16 ? 16 : n;
}

static uint32_t menu_height(void) {
    return (menu_app_rows() + MENU_FOOTER_ROWS) * MENU_ROW_H
        + MENU_SEPARATOR_H + 8;
}

static void queue_exec(const char *exec, const char *builtin) {
    pending_kind = BOTTOM_ACTION_NONE;
    pending_path[0] = '\0';
    pending_builtin[0] = '\0';
    if (exec && exec[0]) {
        pending_kind = BOTTOM_ACTION_EXEC;
        copy_trunc(pending_path, sizeof(pending_path), exec);
    } else if (builtin && builtin[0]) {
        pending_kind = BOTTOM_ACTION_BUILTIN;
        copy_trunc(pending_builtin, sizeof(pending_builtin), builtin);
    }
}

bool bottom_panel_take_action(enum bottom_panel_action *kind,
                              char *path, uint32_t path_cap,
                              char *builtin, uint32_t builtin_cap) {
    if (pending_kind == BOTTOM_ACTION_NONE) return false;
    if (kind) *kind = pending_kind;
    if (path && path_cap) copy_trunc(path, path_cap, pending_path);
    if (builtin && builtin_cap) copy_trunc(builtin, builtin_cap, pending_builtin);
    pending_kind = BOTTOM_ACTION_NONE;
    pending_path[0] = '\0';
    pending_builtin[0] = '\0';
    return true;
}

bool bottom_panel_is_menu_open(void) { return menu_open; }
void bottom_panel_close_menu(void) { menu_open = false; }

bool bottom_panel_needs_redraw(void) {
    const struct desktop_datetime *dt = datetime_service_get();
    if (!dt) return false;
    if (dt->minute != last_minute) {
        // First call arms the cache without forcing redraw storm.
        bool changed = last_minute != 0xFF;
        last_minute = dt->minute;
        return changed;
    }
    return false;
}

static uint32_t mix(uint32_t a, uint32_t b, uint32_t t) {
    uint32_t r = (((a >> 16) & 0xFF) * (255 - t) + ((b >> 16) & 0xFF) * t) / 255;
    uint32_t g = (((a >> 8) & 0xFF) * (255 - t) + ((b >> 8) & 0xFF) * t) / 255;
    uint32_t bl = ((a & 0xFF) * (255 - t) + (b & 0xFF) * t) / 255;
    return (r << 16) | (g << 8) | bl;
}

struct bevel_colors {
    uint32_t face, light, shadow, dark, sunk, text, muted, accent, on_accent;
};

static struct bevel_colors bevel_from(const struct personalization_colors *th) {
    struct bevel_colors c;
    c.face = th->titlebar;
    c.light = mix(th->titlebar, 0xFFFFFF, 70);
    c.shadow = mix(th->titlebar, 0x000000, 90);
    c.dark = mix(th->titlebar, 0x000000, 170);
    c.sunk = mix(th->titlebar, 0x000000, 60);
    c.text = th->text;
    c.muted = th->muted_text;
    c.accent = th->accent;
    c.on_accent = mix(th->accent, 0x000000, 200);
    return c;
}

static void bevel_raised(const struct bevel_colors *c, uint32_t x, uint32_t y,
                         uint32_t w, uint32_t h, uint32_t face) {
    if (w < 4 || h < 4) return;
    display_draw_rect(x, y, w, h, c->dark);
    display_draw_rect(x, y, w - 1, h - 1, c->light);
    display_draw_rect(x + 1, y + 1, w - 2, h - 2, c->shadow);
    display_draw_rect(x + 1, y + 1, w - 3, h - 3, face);
}

static void bevel_sunken(const struct bevel_colors *c, uint32_t x, uint32_t y,
                         uint32_t w, uint32_t h, uint32_t face) {
    if (w < 4 || h < 4) return;
    display_draw_rect(x, y, w, h, c->light);
    display_draw_rect(x, y, w - 1, h - 1, c->dark);
    display_draw_rect(x + 1, y + 1, w - 2, h - 2, c->face);
    display_draw_rect(x + 1, y + 1, w - 3, h - 3, c->shadow);
    display_draw_rect(x + 2, y + 2, w - 4, h - 4, face);
}

static void separator_v(const struct bevel_colors *c, uint32_t x, uint32_t y,
                        uint32_t h) {
    display_draw_rect(x, y, 1, h, c->shadow);
    display_draw_rect(x + 1, y, 1, h, c->light);
}

static void text_bold(uint32_t x, uint32_t y, const char *t, uint32_t fg,
                      uint32_t bg) {
    enum display_font_face old = display_get_font_face();
    display_set_font_face(DISPLAY_FONT_BOLD);
    display_draw_text_at(x, y, t, fg, bg);
    display_set_font_face(old);
}

static void icon_folder(uint32_t x, uint32_t y) {
    display_draw_rect(x + 1, y + 2, 6, 2, 0xC9962E);
    display_draw_rect(x + 1, y + 4, 14, 10, 0xC9962E);
    display_draw_rect(x + 2, y + 6, 12, 7, 0xF2C94C);
    display_draw_rect(x + 2, y + 6, 12, 1, 0xFBE38E);
}

static void icon_terminal(uint32_t x, uint32_t y) {
    display_draw_rect(x, y + 1, 16, 13, 0x8A8F9E);
    display_draw_rect(x + 1, y + 2, 14, 11, 0x101216);
    display_draw_rect(x + 3, y + 4, 1, 1, 0x7EE787);
    display_draw_rect(x + 4, y + 5, 1, 1, 0x7EE787);
    display_draw_rect(x + 5, y + 6, 1, 1, 0x7EE787);
    display_draw_rect(x + 4, y + 7, 1, 1, 0x7EE787);
    display_draw_rect(x + 3, y + 8, 1, 1, 0x7EE787);
    display_draw_rect(x + 7, y + 9, 5, 1, 0x7EE787);
}

static void icon_gear(uint32_t x, uint32_t y) {
    uint32_t c = 0xA9B1C6, d = 0x5C637A;
    display_draw_rect(x + 7, y + 1, 2, 14, c);
    display_draw_rect(x + 1, y + 7, 14, 2, c);
    display_draw_rect(x + 3, y + 3, 10, 10, c);
    display_draw_rect(x + 2, y + 2, 3, 3, c);
    display_draw_rect(x + 11, y + 2, 3, 3, c);
    display_draw_rect(x + 2, y + 11, 3, 3, c);
    display_draw_rect(x + 11, y + 11, 3, 3, c);
    display_draw_rect(x + 6, y + 6, 4, 4, d);
}

static void icon_chip(uint32_t x, uint32_t y) {
    for (uint32_t i = 0; i < 4; i++) {
        display_draw_rect(x + 4 + i * 3, y + 1, 1, 2, 0xC0C4D0);
        display_draw_rect(x + 4 + i * 3, y + 13, 1, 2, 0xC0C4D0);
        display_draw_rect(x + 1, y + 4 + i * 3, 2, 1, 0xC0C4D0);
        display_draw_rect(x + 13, y + 4 + i * 3, 2, 1, 0xC0C4D0);
    }
    display_draw_rect(x + 3, y + 3, 10, 10, 0x2B3040);
    display_draw_rect(x + 4, y + 4, 8, 8, 0x3E8E7E);
    display_draw_rect(x + 5, y + 5, 2, 2, 0xB8F2E6);
}

static void icon_power(uint32_t x, uint32_t y, uint32_t color) {
    display_draw_rect(x + 3, y + 4, 2, 7, color);
    display_draw_rect(x + 11, y + 4, 2, 7, color);
    display_draw_rect(x + 4, y + 11, 8, 2, color);
    display_draw_rect(x + 4, y + 3, 2, 2, color);
    display_draw_rect(x + 10, y + 3, 2, 2, color);
    display_draw_rect(x + 7, y + 1, 2, 7, color);
}

static void icon_logo(uint32_t x, uint32_t y, uint32_t accent) {
    display_draw_rect(x, y, 16, 16, mix(accent, 0x000000, 110));
    display_draw_rect(x + 1, y + 1, 14, 14, accent);
    display_draw_rect(x + 4, y + 3, 2, 10, 0xFFFFFF);
    display_draw_rect(x + 6, y + 3, 5, 2, 0xFFFFFF);
    display_draw_rect(x + 6, y + 7, 5, 2, 0xFFFFFF);
    display_draw_rect(x + 10, y + 4, 2, 4, 0xFFFFFF);
}

static void icon_for_quick(uint32_t i, uint32_t x, uint32_t y) {
    if (i == 0) icon_folder(x, y);
    else if (i == 1) icon_terminal(x, y);
    else if (i == 2) icon_gear(x, y);
    else icon_chip(x, y);
}

static const struct desktop_entry *entry_for_title(const char *title) {
    uint32_t n = desktop_entries_count();
    for (uint32_t i = 0; i < n; i++) {
        const struct desktop_entry *e = desktop_entries_get(i);
        if (!e) continue;
        const char *base = e->exec;
        for (const char *p = e->exec; *p; p++)
            if (*p == '/') base = p + 1;
        uint32_t k = 0;
        bool same_name = true, same_base = base[0] != 0;
        for (; title[k]; k++) {
            char a = title[k], b = e->name[k];
            if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
            if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
            if (a != b) same_name = false;
            char cbase = base[k];
            if (cbase >= 'A' && cbase <= 'Z') cbase = (char)(cbase + 32);
            if (same_base && a != cbase) same_base = false;
            if (!e->name[k]) same_name = false;
            if (!base[k]) same_base = false;
        }
        if (same_name && !e->name[k]) return e;
        if (same_base && !base[k]) return e;
    }
    return 0;
}

static void entry_chip(const struct desktop_entry *e, uint32_t x, uint32_t y,
                       uint32_t accent) {
    uint32_t color = e ? e->icon_color : accent;
    display_draw_rect(x, y, 16, 16, mix(color, 0x000000, 120));
    display_draw_rect(x + 1, y + 1, 14, 14, color);
    display_draw_rect(x + 1, y + 1, 14, 1, mix(color, 0xFFFFFF, 110));
    if (e && e->icon_text[0]) {
        char t[2] = { e->icon_text[0], 0 };
        display_draw_text_at(x + 4, y + 4, t, mix(color, 0x000000, 210), color);
    }
}

static void two(char *out, uint32_t v) {
    out[0] = (char)('0' + (v / 10) % 10);
    out[1] = (char)('0' + v % 10);
}

static uint32_t tasks_area_x(void) {
    return 4 + START_BTN_W + 6 + QUICK_COUNT * QUICK_W + 8;
}

static uint32_t task_width(uint32_t sw, uint32_t count) {
    uint32_t x0 = tasks_area_x();
    uint32_t end = sw > TRAY_W + 10 ? sw - TRAY_W - 10 : x0;
    if (!count || end <= x0) return TASK_W;
    uint32_t room = end - x0;
    uint32_t w = room / count;
    if (w > TASK_W + TASK_GAP) w = TASK_W + TASK_GAP;
    return w > TASK_GAP ? w - TASK_GAP : w;
}

static void draw_tray(uint32_t sw, uint32_t ty, const struct bevel_colors *c) {
    uint32_t tx = sw > TRAY_W + 4 ? sw - TRAY_W - 4 : 4;
    bevel_sunken(c, tx, ty + BTN_Y, TRAY_W, BTN_H, c->face);
    uint32_t x = tx + 8;
    struct battery_info binfo;
    int32_t rc = (int32_t)userspace_syscall(SYS_BATTERY_INFO, (uint64_t)&binfo, 0, 0);
    if (rc >= 0 && binfo.present && binfo.percent <= 100) {
        uint32_t by = ty + BTN_Y + 8;
        uint32_t fill_color = binfo.charging ? 0x5FD07A
            : (binfo.percent <= 15 ? 0xF0605A : c->text);
        display_draw_rect(x, by, 16, 9, c->text);
        display_draw_rect(x + 1, by + 1, 14, 7, c->face);
        display_draw_rect(x + 16, by + 2, 2, 5, c->text);
        uint32_t fill = (12 * binfo.percent + 50) / 100;
        if (fill) display_draw_rect(x + 2, by + 2, fill, 5, fill_color);
        char pct[6];
        uint32_t n = 0, p = binfo.percent;
        if (p >= 100) { pct[n++] = '1'; pct[n++] = '0'; pct[n++] = '0'; }
        else { if (p >= 10) pct[n++] = (char)('0' + p / 10); pct[n++] = (char)('0' + p % 10); }
        pct[n++] = '%';
        pct[n] = 0;
        display_draw_text_at(x + 22, ty + BTN_Y + 8, pct, c->text, c->face);
    }
    const struct desktop_datetime *dt = datetime_service_get();
    char clock[6] = "--:--";
    if (dt) {
        last_minute = dt->minute;
        two(clock, dt->hour);
        clock[2] = ':';
        two(clock + 3, dt->minute);
    }
    display_draw_text_at(tx + TRAY_W - 8 - 40, ty + BTN_Y + 8, clock, c->text, c->face);
}

static void draw_menu(uint32_t sw, uint32_t sh, const struct bevel_colors *c,
                      const struct personalization_colors *th) {
    if (!menu_open) return;
    (void)sw;
    uint32_t mh = menu_height();
    uint32_t mx = 2;
    uint32_t my = sh > BOTTOM_PANEL_HEIGHT + mh ? sh - BOTTOM_PANEL_HEIGHT - mh : 28;
    bevel_raised(c, mx, my, MENU_W, mh, c->face);
    uint32_t bx = mx + 3, bh = mh - 6;
    for (uint32_t i = 0; i < bh; i += 4) {
        uint32_t t = (i * 200) / (bh ? bh : 1);
        display_draw_rect(bx, my + 3 + i, MENU_BANNER_W, 4,
                          mix(mix(c->accent, 0x000000, 150), c->accent, t));
    }
    static const char banner[] = "PureC";
    uint32_t ly = my + mh - 14;
    for (int32_t i = (int32_t)sizeof(banner) - 2; i >= 0; i--) {
        char ch[2] = { banner[i], 0 };
        text_bold(bx + 8, ly, ch, 0xFFFFFF, c->accent);
        ly -= 11;
    }
    uint32_t rx = mx + 3 + MENU_BANNER_W + 2;
    uint32_t rw = MENU_W - MENU_BANNER_W - 8;
    uint32_t row = 0;
    uint32_t n = desktop_entries_count();
    uint32_t apps = menu_app_rows();
    uint32_t y = my + 4;
    for (uint32_t i = 0; i < n && row < apps; i++) {
        const struct desktop_entry *e = desktop_entries_get(i);
        if (!e || e->hidden) continue;
        bool hot = menu_hover == (int32_t)row;
        uint32_t bg = hot ? c->accent : c->face;
        display_draw_rect(rx, y, rw, MENU_ROW_H, bg);
        entry_chip(e, rx + 6, y + 4, c->accent);
        display_draw_text_at(rx + 30, y + 8, e->name, hot ? c->on_accent : c->text, bg);
        y += MENU_ROW_H;
        row++;
    }
    display_draw_rect(rx + 4, y + 3, rw - 8, 1, c->shadow);
    display_draw_rect(rx + 4, y + 4, rw - 8, 1, c->light);
    y += MENU_SEPARATOR_H;
    for (uint32_t k = 0; k < MENU_FOOTER_ROWS; k++) {
        bool hot = menu_hover == (int32_t)(apps + k);
        uint32_t bg = hot ? (k == 1 ? th->danger : c->accent) : c->face;
        display_draw_rect(rx, y, rw, MENU_ROW_H, bg);
        icon_power(rx + 6, y + 4, hot ? c->on_accent : (k == 1 ? th->danger : c->text));
        display_draw_text_at(rx + 30, y + 8, k == 0 ? "Restart" : "Shut down",
                             hot ? c->on_accent : (k == 1 ? th->danger : c->text), bg);
        y += MENU_ROW_H;
    }
}

void bottom_panel_draw(uint32_t screen_width, uint32_t screen_height) {
    if (!screen_width || !screen_height) return;
    struct personalization_colors th;
    personalization_current_colors(&th);
    struct bevel_colors c = bevel_from(&th);
    uint32_t ty = screen_height > BOTTOM_PANEL_HEIGHT
        ? screen_height - BOTTOM_PANEL_HEIGHT : 0;
    display_draw_rect(0, ty, screen_width, BOTTOM_PANEL_HEIGHT, c.face);
    display_draw_rect(0, ty, screen_width, 1, c.face);
    display_draw_rect(0, ty + 1, screen_width, 1, c.light);

    bool down = menu_open || start_pressed_look;
    if (down) bevel_sunken(&c, 3, ty + BTN_Y, START_BTN_W, BTN_H, c.face);
    else bevel_raised(&c, 3, ty + BTN_Y, START_BTN_W, BTN_H, c.face);
    uint32_t o = down ? 1 : 0;
    icon_logo(9 + o, ty + BTN_Y + 4 + o, c.accent);
    text_bold(31 + o, ty + BTN_Y + 8 + o, "PureC", c.text, c.face);

    uint32_t qx = 4 + START_BTN_W + 4;
    separator_v(&c, qx, ty + BTN_Y + 2, BTN_H - 4);
    qx += 4;
    for (uint32_t i = 0; i < QUICK_COUNT; i++) {
        icon_for_quick(i, qx + 4, ty + BTN_Y + 4);
        qx += QUICK_W;
    }
    separator_v(&c, qx + 2, ty + BTN_Y + 2, BTN_H - 4);

    struct gui_task_info tasks[MAX_TASKS];
    uint32_t count = window_manager_tasks(tasks, MAX_TASKS);
    if (count > MAX_TASKS) count = MAX_TASKS;
    uint32_t tw = task_width(screen_width, count);
    if (tw < TASK_MIN_W && count) {
        uint32_t fit = 0;
        uint32_t x0 = tasks_area_x();
        uint32_t end = screen_width > TRAY_W + 10 ? screen_width - TRAY_W - 10 : x0;
        if (end > x0) fit = (end - x0) / (TASK_MIN_W + TASK_GAP);
        if (count > fit) count = fit;
        tw = TASK_MIN_W;
    }
    uint32_t x = tasks_area_x();
    for (uint32_t i = 0; i < count; i++) {
        bool focus = (tasks[i].flags & GUI_TASK_FOCUSED) != 0;
        bool mini = (tasks[i].flags & GUI_TASK_MINIMIZED) != 0;
        uint32_t face = focus ? mix(c.face, 0xFFFFFF, 22) : c.face;
        if (focus) bevel_sunken(&c, x, ty + BTN_Y, tw, BTN_H, face);
        else bevel_raised(&c, x, ty + BTN_Y, tw, BTN_H, face);
        uint32_t off = focus ? 1 : 0;
        const struct desktop_entry *e = entry_for_title(tasks[i].name);
        entry_chip(e, x + 5 + off, ty + BTN_Y + 4 + off, c.accent);
        uint32_t max_chars = tw > 36 ? (tw - 34) / 8 : 0;
        char label[32];
        label[0] = 0;
        if (max_chars >= 4)
            truncate_middle(tasks[i].name[0] ? tasks[i].name : "window", label,
                            sizeof(label), max_chars);
        uint32_t fg = mini ? c.muted : c.text;
        if (focus) text_bold(x + 27 + off, ty + BTN_Y + 8 + off, label, fg, face);
        else display_draw_text_at(x + 27, ty + BTN_Y + 8, label, fg, face);
        x += tw + TASK_GAP;
    }

    draw_tray(screen_width, ty, &c);
    draw_menu(screen_width, screen_height, &c, &th);
}

void top_panel_draw(uint32_t screen_width, const char *focused_title) {
    struct personalization_colors th;
    personalization_current_colors(&th);
    struct bevel_colors c = bevel_from(&th);
    display_draw_rect(0, 0, screen_width, TOP_PANEL_HEIGHT, c.face);
    display_draw_rect(0, TOP_PANEL_HEIGHT - 2, screen_width, 1, c.shadow);
    display_draw_rect(0, TOP_PANEL_HEIGHT - 1, screen_width, 1, c.dark);
    icon_logo(8, 6, c.accent);
    text_bold(30, 10, "PureC OS", c.text, c.face);
    uint32_t x = 30 + 8 * 8 + 10;
    if (focused_title && focused_title[0]) {
        separator_v(&c, x, 6, 16);
        char t[40];
        truncate_middle(focused_title, t, sizeof(t), 32);
        display_draw_text_at(x + 10, 10, t, c.muted, c.face);
    }
    const struct desktop_datetime *dt = datetime_service_get();
    if (dt && screen_width > 520) {
        static const char *const months[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                                "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
        static const char *const days[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
        uint32_t y = dt->year, m = dt->month, d = dt->day;
        if (m < 3) { m += 12; y -= 1; }
        uint32_t wd = (d + (13 * (m + 1)) / 5 + y + y / 4 - y / 100 + y / 400 + 6) % 7;
        char line[32];
        uint32_t n = 0;
        const char *dn = days[wd];
        while (*dn) line[n++] = *dn++;
        line[n++] = ' ';
        if (dt->day >= 10) line[n++] = (char)('0' + dt->day / 10);
        line[n++] = (char)('0' + dt->day % 10);
        line[n++] = ' ';
        const char *mn = months[(dt->month + 11) % 12];
        while (*mn) line[n++] = *mn++;
        line[n++] = ' ';
        line[n++] = ' ';
        two(line + n, dt->hour); n += 2;
        line[n++] = ':';
        two(line + n, dt->minute); n += 2;
        line[n] = 0;
        uint32_t w = n * 8;
        text_bold((screen_width - w) / 2, 10, line, c.text, c.face);
    }
    uint32_t px = screen_width > TOP_POWER_W + 4 ? screen_width - TOP_POWER_W - 4 : 0;
    bevel_raised(&c, px, 3, TOP_POWER_W, 22, c.face);
    icon_power(px + (TOP_POWER_W - 16) / 2, 6, th.danger);
}

void top_panel_power_rect(uint32_t screen_width, uint32_t *x, uint32_t *y,
                          uint32_t *w, uint32_t *h) {
    *x = screen_width > TOP_POWER_W + 4 ? screen_width - TOP_POWER_W - 4 : 0;
    *y = 3;
    *w = TOP_POWER_W;
    *h = 22;
}

bool bottom_panel_handle_mouse(int32_t x, int32_t y, uint8_t buttons,
                               bool pressed, bool released,
                               uint32_t screen_width, uint32_t screen_height,
                               bool *redraw_required) {
    (void)buttons; (void)released;
    if (redraw_required) *redraw_required = false;
    if (!screen_width || !screen_height) return false;
    uint32_t ty = screen_height > BOTTOM_PANEL_HEIGHT
        ? screen_height - BOTTOM_PANEL_HEIGHT : 0;

    if (menu_open) {
        uint32_t mh = menu_height();
        uint32_t mx = 2;
        uint32_t my = screen_height > BOTTOM_PANEL_HEIGHT + mh
            ? screen_height - BOTTOM_PANEL_HEIGHT - mh : 28;
        uint32_t apps = menu_app_rows();
        int32_t hover = -1;
        if (point_in(x, y, mx + MENU_BANNER_W + 5, my + 4,
                     MENU_W - MENU_BANNER_W - 8, mh - 8)) {
            int32_t rel = y - (int32_t)(my + 4);
            int32_t apps_h = (int32_t)(apps * MENU_ROW_H);
            if (rel < apps_h) hover = rel / MENU_ROW_H;
            else if (rel >= apps_h + MENU_SEPARATOR_H) {
                int32_t k = (rel - apps_h - MENU_SEPARATOR_H) / MENU_ROW_H;
                if (k < MENU_FOOTER_ROWS) hover = (int32_t)apps + k;
            }
        }
        if (hover != menu_hover) {
            menu_hover = hover;
            if (redraw_required) *redraw_required = true;
        }
        if (point_in(x, y, mx, my, MENU_W, mh)) {
            if (pressed && hover >= 0) {
                if ((uint32_t)hover < apps) {
                    uint32_t n = desktop_entries_count();
                    uint32_t vis = 0;
                    for (uint32_t i = 0; i < n; i++) {
                        const struct desktop_entry *e = desktop_entries_get(i);
                        if (!e || e->hidden) continue;
                        if (vis == (uint32_t)hover) {
                            queue_exec(e->exec, e->builtin);
                            break;
                        }
                        vis++;
                    }
                } else {
                    pending_kind = (uint32_t)hover == apps
                        ? BOTTOM_ACTION_REBOOT : BOTTOM_ACTION_SHUTDOWN;
                }
                menu_open = false;
                menu_hover = -1;
                if (redraw_required) *redraw_required = true;
            }
            return true;
        }
        if (pressed && !point_in(x, y, 0, ty, screen_width, BOTTOM_PANEL_HEIGHT)) {
            menu_open = false;
            menu_hover = -1;
            if (redraw_required) *redraw_required = true;
            return false;
        }
    }

    if (!point_in(x, y, 0, ty, screen_width, BOTTOM_PANEL_HEIGHT))
        return false;

    if (pressed) {
        if (point_in(x, y, 3, ty + BTN_Y, START_BTN_W, BTN_H)) {
            menu_open = !menu_open;
            menu_hover = -1;
            if (redraw_required) *redraw_required = true;
            return true;
        }
        uint32_t qx = 4 + START_BTN_W + 8;
        for (uint32_t i = 0; i < QUICK_COUNT; i++) {
            if (point_in(x, y, qx, ty + BTN_Y, QUICK_W, BTN_H)) {
                queue_exec(quick_exec[i], "");
                (void)quick_tip;
                menu_open = false;
                if (redraw_required) *redraw_required = true;
                return true;
            }
            qx += QUICK_W;
        }
        struct gui_task_info tasks[MAX_TASKS];
        uint32_t count = window_manager_tasks(tasks, MAX_TASKS);
        if (count > MAX_TASKS) count = MAX_TASKS;
        uint32_t tw = task_width(screen_width, count);
        if (tw < TASK_MIN_W) tw = TASK_MIN_W;
        uint32_t bx = tasks_area_x();
        for (uint32_t i = 0; i < count; i++) {
            if (point_in(x, y, bx, ty + BTN_Y, tw, BTN_H)) {
                window_manager_focus_pid(tasks[i].pid);
                menu_open = false;
                if (redraw_required) *redraw_required = true;
                return true;
            }
            bx += tw + TASK_GAP;
        }
        if (menu_open) {
            menu_open = false;
            if (redraw_required) *redraw_required = true;
        }
        return true;
    }
    return true;
}

uint32_t panel_face_color(void) {
    struct personalization_colors th;
    personalization_current_colors(&th);
    return th.titlebar;
}

uint32_t panel_text_color(bool muted) {
    struct personalization_colors th;
    personalization_current_colors(&th);
    return muted ? th.muted_text : th.text;
}

void panel_bevel_button(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                        bool pressed) {
    struct personalization_colors th;
    personalization_current_colors(&th);
    struct bevel_colors c = bevel_from(&th);
    if (pressed) bevel_sunken(&c, x, y, w, h, c.face);
    else bevel_raised(&c, x, y, w, h, c.face);
}

void top_panel_draw_power_menu(uint32_t screen_width) {
    struct personalization_colors th;
    personalization_current_colors(&th);
    struct bevel_colors c = bevel_from(&th);
    uint32_t x = screen_width > 158 ? screen_width - 158 : 0;
    bevel_raised(&c, x, 28, 150, 62, c.face);
    icon_power(x + 8, 35, c.text);
    display_draw_text_at(x + 32, 39, "Restart", c.text, c.face);
    display_draw_rect(x + 6, 58, 138, 1, c.shadow);
    display_draw_rect(x + 6, 59, 138, 1, c.light);
    icon_power(x + 8, 65, th.danger);
    display_draw_text_at(x + 32, 69, "Shut down", th.danger, c.face);
}
