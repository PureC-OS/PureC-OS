#include "../../libgui/include/puregui.h"
#include "../../libgui/include/pguiw.h"
#include "../../libc/include/purec.h"
#include "../../libfs/include/purefs.h"

#define DISK_LIMIT 12
#define MIB (1024ULL*1024ULL)
#define GIB (1024ULL*1024ULL*1024ULL)

struct disk_app {
    struct storage_device_info disks[DISK_LIMIT];
    int32_t count;
    int32_t selected;
    bool confirm_format;
    char status[96];
};

static char *append_text(char *out, const char *text) {
    while (*text) *out++ = *text++;
    *out = '\0';
    return out;
}

static char *append_u64(char *out, uint64_t v) {
    char rev[24]; uint32_t c = 0;
    do { rev[c++] = (char)('0' + v % 10U); v /= 10U; } while (v && c < sizeof(rev));
    while (c) *out++ = rev[--c];
    *out = '\0';
    return out;
}

static void refresh(struct disk_app *app) {
    int32_t keep = app->selected;
    app->count = pc_list_disks(app->disks, DISK_LIMIT);
    if (app->count < 0) app->count = 0;
    if (keep >= 0 && keep < app->count) app->selected = keep;
    else { if (app->selected >= app->count) app->selected = app->count - 1; if (app->selected < 0 && app->count > 0) app->selected = 0; }
}

static void disk_line(char *line, const struct storage_device_info *d) {
    char *out = append_text(line, d->name);
    out = append_text(out, "  "); out = append_text(out, d->model[0] ? d->model : "Unknown");
    out = append_text(out, "  ");
    uint64_t bytes = d->sector_count * d->sector_size;
    if (bytes >= GIB) { out = append_u64(out, bytes / GIB); out = append_text(out, " GiB"); }
    else { out = append_u64(out, bytes / MIB); out = append_text(out, " MiB"); }
    out = append_text(out, d->operational ? "  online" : "  offline");
    (void)append_text(out, d->writable ? "  writable" : "  read-only");
}

static void draw(struct pg_window *win, struct disk_app *app, const struct pg_event *ev) {
    pg_window_begin(win);
    pg_window_text(win, 20, 18, "Storage overview", win->theme.text);
    char summary[64]; char *out = append_text(summary, "Connected disks: ");
    out = append_u64(out, (uint32_t)app->count);
    uint64_t total = 0;
    for (int32_t i = 0; i < app->count; i++) total += app->disks[i].sector_count * app->disks[i].sector_size;
    out = append_text(out, "    Total: "); out = append_u64(out, total / GIB); (void)append_text(out, " GiB");
    pg_window_text(win, 20, 40, summary, win->theme.muted_text);

    pg_window_text(win, 20, 66, "Connected devices", win->theme.text);
    uint32_t controls = win->client.height - 170;
    uint32_t visible_rows = controls > 90 ? (controls - 90) / 30U : 0;
    int32_t vis = app->count;
    if ((uint32_t)vis > visible_rows) vis = (int32_t)visible_rows;
    for (int32_t i = 0; i < vis; i++) {
        struct pg_rect row = {20, 88 + (uint32_t)i * 30, win->client.width - 40, 26};
        pg_window_rect(win, row, i == app->selected ? 0x45475A : 0x2B2D40);
        char line[120]; disk_line(line, &app->disks[i]);
        pg_window_text(win, row.x + 8, row.y + 9, line, win->theme.text);
        if (ev && ev->type == PG_EVENT_MOUSE_UP && ev->button == 1) {
            int32_t x = ev->x - (int32_t)win->client.x;
            int32_t y = ev->y - (int32_t)win->client.y;
            if (x >= (int32_t)row.x && x < (int32_t)(row.x + row.width) && y >= (int32_t)row.y && y < (int32_t)(row.y + row.height)) {
                app->selected = i; app->confirm_format = false; app->status[0] = '\0';
            }
        }
    }

    if (app->selected >= 0) {
        const struct storage_device_info *d = &app->disks[app->selected];
        char sel[96]; out = append_text(sel, "Selected: "); out = append_text(out, d->name); out = append_text(out, "  serial: "); (void)append_text(out, d->serial[0] ? d->serial : "unavailable");
        pg_window_text(win, 20, controls, sel, win->theme.text);

        uint32_t y = controls + 22;
        pg_window_text(win, 20, y, "Filesystem:", win->theme.muted_text);
        const char *fs_name = "EXT2";
        pg_window_text(win, 140, y, fs_name, win->theme.text);
        if (pg_button(win, (struct pg_rect){200, y - 4, 80, 22}, "EXT2 only", ev)) {
            app->confirm_format = false;
        }

        y += 26;
        y = win->client.height - 52;
        const char *label = app->confirm_format ? "CONFIRM ERASE" : "Format";
        char btn_label[32]; char *bl = append_text(btn_label, label);
        (void)append_text(bl, " EXT2");
        if (pg_button(win, (struct pg_rect){20, y, 170, 30}, btn_label, ev)) {
            if (!d->writable || !d->operational || !d->serial[0]) {
                pc_copy(app->status, "Disk cannot be formatted safely.", sizeof(app->status));
                app->confirm_format = false;
            } else if (!app->confirm_format) {
                app->confirm_format = true;
                pc_copy(app->status, "Warning: confirmation erases disk.", sizeof(app->status));
            } else {
                int32_t r = pc_format_device_ex(d->name, d->serial, FS_TYPE_EXT2);
                app->confirm_format = false;
                if (r == 0) pc_copy(app->status, "EXT2 format done.", sizeof(app->status));
                else { char msg[96]; pc_copy(msg, pf_strerror(r), sizeof(msg)); pc_copy(app->status, msg, sizeof(app->status)); if (r == PF_ERROR_BUSY) pc_copy(app->status, "busy - system disk, pick another", sizeof(app->status)); }
                refresh(app);
            }
        }
        if (app->status[0]) pg_window_text(win, 200, y + 8, app->status, app->confirm_format ? win->theme.danger : win->theme.muted_text);
    }
    pg_window_end(win);
}

static int disks_main(void) {
    struct pg_window win;
    struct pc_display_info di;
    if (!pc_display_get_info(&di) || !di.available) return 1;
    uint32_t w = di.width > 780 ? 760 : di.width - 20;
    uint32_t h = di.height > 600 ? 560 : di.height - 40;
    if (!pg_window_center(&win, "Disks", w, h)) return 1;
    struct disk_app app = {.selected = -1};
    refresh(&app);
    struct pg_event ev = {0};
    draw(&win, &app, &ev);
    uint32_t elapsed = 0;
    while (pg_window_is_open(&win)) {
        if (pg_window_poll_event(&win, &ev)) {
            if (ev.type == PG_EVENT_CLOSE) break;
            if (ev.type != PG_EVENT_MOUSE_MOVE) draw(&win, &app, &ev);
        }
        pc_sleep(20); elapsed += 20;
        if (elapsed >= 1000) { elapsed = 0; refresh(&app); draw(&win, &app, 0); }
    }
    pg_window_close(&win);
    return 0;
}

void _start(void) { pc_exit(disks_main()); }
