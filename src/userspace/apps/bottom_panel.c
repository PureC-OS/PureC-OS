#include "bottom_panel.h"
#include "desktop_entries.h"
#include "datetime_service.h"
#include "../display.h"
#include "../personalization.h"
#include "../window_manager.h"
#include "../../kernel/process/process.h"
#include "../syscall.h"
#include "../../kernel/syscall/syscall.h"
#include "../../lib/string.h"

#define START_BTN_W 86
#define QUICK_W 32
#define TASK_W 128
#define TASK_GAP 4
#define TRAY_W 124
#define TRAY_CLOCK_X 8
#define TRAY_BAT_X 60
#define MENU_W 248
#define MENU_ROW_H 26
#define MENU_FOOTER_ROWS 2

static bool menu_open = false;
static enum bottom_panel_action pending_kind = BOTTOM_ACTION_NONE;
static char pending_path[128];
static char pending_builtin[16];
static uint8_t last_minute = 0xFF;

static const char *quick_exec[4] = {
    "/bin/program/files",
    "/bin/program/terminal",
    "/bin/program/settings",
    "/bin/program/devmgr"
};
static const char *quick_label[4] = { "FL", "T>", "{ }", "DV" };

uint32_t bottom_panel_height(void) { return BOTTOM_PANEL_HEIGHT; }

void bottom_panel_init(void) {
    menu_open = false;
    pending_kind = BOTTOM_ACTION_NONE;
    pending_path[0] = '\0';
    pending_builtin[0] = '\0';
    last_minute = 0xFF;
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

static void task_name_for_pid(uint32_t pid, char *out, uint32_t cap) {
    #define TASKBAR_LOOKUP_CAP 64
    struct process_monitor_info list[TASKBAR_LOOKUP_CAP];
    int32_t n = process_monitor_list(list, TASKBAR_LOOKUP_CAP);
    if (n > 0) {
        for (int32_t i = 0; i < n; i++) {
            if (list[i].pid == pid && list[i].name[0]) {
                const char *name = list[i].name;
                const char *slash = name;
                for (const char *p = name; *p; p++)
                    if (*p == '/') slash = p + 1;
                copy_trunc(out, cap, slash[0] ? slash : name);
                return;
            }
        }
    }
    char tmp[16];
    uint32_t len = 0;
    tmp[len++] = 'P'; tmp[len++] = 'I'; tmp[len++] = 'D'; tmp[len++] = ' ';
    char rev[10]; uint32_t rc = 0; uint32_t v = pid;
    do { rev[rc++] = (char)('0' + v % 10); v /= 10; } while (v && rc < 10);
    while (rc && len < sizeof(tmp) - 1) tmp[len++] = rev[--rc];
    tmp[len] = '\0';
    copy_trunc(out, cap, tmp);
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

static uint32_t menu_height(void) {
    uint32_t rows = menu_visible_count() + MENU_FOOTER_ROWS;
    if (!rows) rows = MENU_FOOTER_ROWS;
    if (rows > 14) rows = 14;
    return rows * MENU_ROW_H + 10;
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

static void draw_clock(uint32_t sw, uint32_t sh,
                       const struct personalization_colors *th) {
    const struct desktop_datetime *dt = datetime_service_get();
    if (dt) last_minute = dt->minute;
    char clock[8];
    if (dt) {
        clock[0] = (char)('0' + (dt->hour / 10));
        clock[1] = (char)('0' + (dt->hour % 10));
        clock[2] = ':';
        clock[3] = (char)('0' + (dt->minute / 10));
        clock[4] = (char)('0' + (dt->minute % 10));
        clock[5] = '\0';
    } else {
        clock[0] = '-'; clock[1] = '-'; clock[2] = ':';
        clock[3] = '-'; clock[4] = '-'; clock[5] = '\0';
    }
    uint32_t tx = sw > TRAY_W + 8 ? sw - TRAY_W - 4 : 4;
    uint32_t ty = sh > BOTTOM_PANEL_HEIGHT ? sh - BOTTOM_PANEL_HEIGHT : 0;
    display_draw_rect(tx, ty + 3, TRAY_W, 24, th->window);
    display_draw_text_at(tx + TRAY_CLOCK_X, ty + 10, clock, th->text, th->window);
}

static void format_bat_number(uint32_t p, char *out, uint32_t cap) {
    if (cap < 4) {
        if (cap) out[0] = '\0';
        return;
    }
    if (p == BATTERY_PERCENT_UNKNOWN || p > 100) {
        if (p > 100 && p != BATTERY_PERCENT_UNKNOWN) p = 100;
        if (p == BATTERY_PERCENT_UNKNOWN) {
            out[0] = '?'; out[1] = '?'; out[2] = '?'; out[3] = '\0';
            return;
        }
    }
    if (p >= 100) {
        out[0] = '1'; out[1] = '0'; out[2] = '0'; out[3] = '\0';
    } else if (p >= 10) {
        out[0] = (char)('0' + (p / 10));
        out[1] = (char)('0' + (p % 10));
        out[2] = '\0';
    } else {
        out[0] = (char)('0' + p);
        out[1] = '\0';
    }
}

static void draw_battery(uint32_t sw, uint32_t sh,
                         const struct personalization_colors *th) {
    uint32_t tx = sw > TRAY_W + 8 ? sw - TRAY_W - 4 : 4;
    uint32_t ty = sh > BOTTOM_PANEL_HEIGHT ? sh - BOTTOM_PANEL_HEIGHT : 0;
    uint32_t bx = tx + TRAY_BAT_X;
    uint32_t bw = (TRAY_W > TRAY_BAT_X + 6) ? (TRAY_W - TRAY_BAT_X - 6) : 24;
    // Clear only battery field so clock digits are never overwritten.
    display_draw_rect(bx, ty + 3, bw, 20, th->window);
    struct battery_info binfo;
    int32_t rc = (int32_t)userspace_syscall(SYS_BATTERY_INFO, (uint64_t)&binfo, 0, 0);
    if (rc < 0 || !binfo.present) {
        display_draw_text_at(bx + 4, ty + 10, "--%", th->muted_text, th->window);
        return;
    }
    char bat_num[5];
    format_bat_number(binfo.percent, bat_num, sizeof(bat_num));
    uint32_t num_len = (uint32_t)strlen(bat_num);
    uint32_t x = bx + 4;
    display_draw_text_at(x, ty + 10, bat_num, th->text, th->window);
    x += num_len * 8;
    display_draw_text_at(x, ty + 10, "%", th->text, th->window);
    x += 8;
    if (binfo.charging) {
        display_draw_text_at(x + 2, ty + 10, "^", th->accent, th->window);
    }
}

static void draw_menu(uint32_t sw, uint32_t sh,
                      const struct personalization_colors *th) {
    if (!menu_open) return;
    uint32_t mh = menu_height();
    uint32_t mx = 4;
    uint32_t my = sh > BOTTOM_PANEL_HEIGHT + mh ? sh - BOTTOM_PANEL_HEIGHT - mh - 4 : 28;
    (void)sw;
    display_draw_rect(mx, my, MENU_W, mh, th->border);
    display_draw_rect(mx + 1, my + 1, MENU_W - 2, mh - 2, th->window);

    uint32_t row = 0;
    uint32_t n = desktop_entries_count();
    uint32_t shown = 0;
    for (uint32_t i = 0; i < n && shown < 12; i++) {
        const struct desktop_entry *e = desktop_entries_get(i);
        if (!e || e->hidden) continue;
        uint32_t ry = my + 5 + row * MENU_ROW_H;
        display_draw_rect(mx + 5, ry, MENU_W - 10, MENU_ROW_H - 2, th->titlebar);
        // color chip
        display_draw_rect(mx + 11, ry + 5, 16, 14, e->icon_color);
        if (e->icon_text[0])
            display_draw_text_at(mx + 12, ry + 8, e->icon_text, 0x1E1E2E, e->icon_color);
        display_draw_text_at(mx + 33, ry + 7, e->name, th->text, th->titlebar);
        row++; shown++;
    }
    // footer: Restart / Power off
    uint32_t ry = my + 5 + row * MENU_ROW_H;
    display_draw_rect(mx + 5, ry, MENU_W - 10, MENU_ROW_H - 2, th->titlebar);
    display_draw_text_at(mx + 33, ry + 7, "Restart", th->text, th->titlebar);
    row++;
    ry = my + 5 + row * MENU_ROW_H;
    display_draw_rect(mx + 5, ry, MENU_W - 10, MENU_ROW_H - 2, th->titlebar);
    display_draw_text_at(mx + 33, ry + 7, "Power off", th->danger, th->titlebar);
}

void bottom_panel_draw(uint32_t screen_width, uint32_t screen_height) {
    if (!screen_width || !screen_height) return;
    struct personalization_colors th;
    personalization_current_colors(&th);

    uint32_t ty = screen_height > BOTTOM_PANEL_HEIGHT
        ? screen_height - BOTTOM_PANEL_HEIGHT : 0;
    // Serenity-like: light separator on top, main bar below.
    display_draw_rect(0, ty, screen_width, 1, th.border);
    display_draw_rect(0, ty + 1, screen_width, BOTTOM_PANEL_HEIGHT - 1, th.titlebar);

    // Start button
    display_draw_rect(4, ty + 4, START_BTN_W, 22,
                      menu_open ? th.accent : th.border);
    display_draw_rect(8, ty + 9, 12, 12, th.accent);
    display_draw_text_at(10, ty + 11, "P", 0x1E1E2E, th.accent);
    display_draw_text_at(26, ty + 10, menu_open ? "Apps v" : "Apps >",
                         menu_open ? 0x1E1E2E : th.text,
                         menu_open ? th.accent : th.border);

    // Quick launch
    uint32_t qx = 4 + START_BTN_W + 8;
    for (uint32_t i = 0; i < 4; i++) {
        if (qx + QUICK_W > screen_width) break;
        display_draw_rect(qx, ty + 4, QUICK_W - 4, 22, th.window);
        display_draw_text_at(qx + 7, ty + 10, quick_label[i], th.text, th.window);
        qx += QUICK_W;
    }

    // Tasks
    uint32_t task_x = qx + 6;
    uint32_t task_pids[64];
    uint32_t ntasks = window_manager_list(task_pids, 0, 64);
    uint32_t focused = window_manager_focused_pid();
    uint32_t max_tasks = 0;
    if (screen_width > task_x + TRAY_W + 16)
        max_tasks = (screen_width - task_x - TRAY_W - 16) / (TASK_W + TASK_GAP);
    if (max_tasks > 64) max_tasks = 64;
    if (ntasks > max_tasks) ntasks = max_tasks;
    for (uint32_t i = 0; i < ntasks; i++) {
        char name[32]; char short_name[20];
        task_name_for_pid(task_pids[i], name, sizeof(name));
        truncate_middle(name, short_name, sizeof(short_name), 14);
        bool is_focus = (task_pids[i] == focused);
        uint32_t bx = task_x + i * (TASK_W + TASK_GAP);
        display_draw_rect(bx, ty + 4, TASK_W, 22,
                          is_focus ? th.accent : th.window);
        // left activity dot
        display_draw_rect(bx + 5, ty + 12, 6, 6,
                          is_focus ? 0x1E1E2E : th.accent);
        display_draw_text_at(bx + 15, ty + 10, short_name,
                             is_focus ? 0x1E1E2E : th.text,
                             is_focus ? th.accent : th.window);
    }
    if (ntasks == 0) {
        display_draw_text_at(task_x + 4, ty + 10, "No windows",
                             th.muted_text, th.titlebar);
    }

    draw_clock(screen_width, screen_height, &th);
    draw_battery(screen_width, screen_height, &th);
    draw_menu(screen_width, screen_height, &th);
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

    // Menu open: clicks inside menu launch entries.
    if (menu_open) {
        uint32_t mh = menu_height();
        uint32_t mx = 4;
        uint32_t my = screen_height > BOTTOM_PANEL_HEIGHT + mh
            ? screen_height - BOTTOM_PANEL_HEIGHT - mh - 4 : 28;
        if (pressed && point_in(x, y, mx, my, MENU_W, mh)) {
            uint32_t rel = (uint32_t)((int32_t)y - (int32_t)(my + 5));
            uint32_t row = rel / MENU_ROW_H;
            // Map visible rows.
            uint32_t n = desktop_entries_count();
            uint32_t vis = 0;
            for (uint32_t i = 0; i < n && vis < 12; i++) {
                const struct desktop_entry *e = desktop_entries_get(i);
                if (!e || e->hidden) continue;
                if (vis == row) {
                    queue_exec(e->exec, e->builtin);
                    menu_open = false;
                    if (redraw_required) *redraw_required = true;
                    return true;
                }
                vis++;
            }
            if (row == vis) {
                pending_kind = BOTTOM_ACTION_REBOOT;
                menu_open = false;
                if (redraw_required) *redraw_required = true;
                return true;
            }
            if (row == vis + 1) {
                pending_kind = BOTTOM_ACTION_SHUTDOWN;
                menu_open = false;
                if (redraw_required) *redraw_required = true;
                return true;
            }
            return true;
        }
        if (pressed && !point_in(x, y, 0, ty, screen_width, BOTTOM_PANEL_HEIGHT)) {
            menu_open = false;
            if (redraw_required) *redraw_required = true;
            return false;
        }
    }

    if (!point_in(x, y, 0, ty, screen_width, BOTTOM_PANEL_HEIGHT))
        return false;

    if (pressed) {
        // Start button
        if (point_in(x, y, 4, ty + 4, START_BTN_W, 22)) {
            menu_open = !menu_open;
            if (redraw_required) *redraw_required = true;
            return true;
        }
        // Quick launch
        uint32_t qx = 4 + START_BTN_W + 8;
        for (uint32_t i = 0; i < 4; i++) {
            if (point_in(x, y, qx, ty + 4, QUICK_W - 4, 22)) {
                queue_exec(quick_exec[i], "");
                menu_open = false;
                if (redraw_required) *redraw_required = true;
                return true;
            }
            qx += QUICK_W;
        }
        // Tasks: focus window
        uint32_t task_x = qx + 6;
        uint32_t task_pids[64];
        uint32_t ntasks = window_manager_list(task_pids, 0, 64);
        uint32_t max_tasks = 0;
        if (screen_width > task_x + TRAY_W + 16)
            max_tasks = (screen_width - task_x - TRAY_W - 16) / (TASK_W + TASK_GAP);
        if (max_tasks > 64) max_tasks = 64;
        if (ntasks > max_tasks) ntasks = max_tasks;
        for (uint32_t i = 0; i < ntasks; i++) {
            uint32_t bx = task_x + i * (TASK_W + TASK_GAP);
            if (point_in(x, y, bx, ty + 4, TASK_W, 22)) {
                window_manager_focus_pid(task_pids[i]);
                menu_open = false;
                if (redraw_required) *redraw_required = true;
                return true;
            }
        }
        // Tray/clock: swallow clicks to avoid desktop deselect.
        if (redraw_required) *redraw_required = false;
        return true;
    }
    // Hover without press still belongs to panel (avoid icon drag-through).
    return true;
}
