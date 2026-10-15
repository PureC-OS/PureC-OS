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
    uint8_t fs_type;
    bool confirm_format;
    char status[96];
    int32_t partition_count;
    uint64_t sizes_gb[4];
    bool custom_format;
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
    uint8_t keep_fs = app->fs_type;
    app->count = pc_list_disks(app->disks, DISK_LIMIT);
    if (app->count < 0) app->count = 0;
    if (keep >= 0 && keep < app->count) app->selected = keep;
    else { if (app->selected >= app->count) app->selected = app->count - 1; if (app->selected < 0 && app->count > 0) app->selected = 0; }
    app->fs_type = keep_fs;
    if (app->partition_count < 1) app->partition_count = 1;
    if (app->partition_count > 4) app->partition_count = 4;
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
        const char *fs_name = app->fs_type == FS_TYPE_EXT2 ? "EXT2" : "FAT32";
        pg_window_text(win, 140, y, fs_name, win->theme.text);
        if (pg_button(win, (struct pg_rect){200, y - 4, 80, 22}, app->fs_type == FS_TYPE_EXT2 ? "-> FAT32" : "-> EXT2", ev)) {
            app->fs_type = (app->fs_type == FS_TYPE_EXT2) ? FS_TYPE_FAT32 : FS_TYPE_EXT2;
            app->confirm_format = false;
        }

        y += 26;
        pg_window_text(win, 20, y, "Partitions:", win->theme.muted_text);
        char pc_s[8]; pc_s[0] = (char)('0' + app->partition_count); pc_s[1] = '\0';
        pg_window_text(win, 140, y, pc_s, win->theme.text);
        if (pg_button(win, (struct pg_rect){170, y - 4, 28, 22}, "-", ev)) if (app->partition_count > 1) app->partition_count--;
        if (pg_button(win, (struct pg_rect){204, y - 4, 28, 22}, "+", ev)) if (app->partition_count < 4) app->partition_count++;
        if (pg_button(win, (struct pg_rect){250, y - 4, 90, 22}, app->custom_format ? "Simple" : "Custom", ev)) app->custom_format = !app->custom_format;

        if (app->custom_format) {
            y += 26;
            pg_window_text(win, 20, y, "Size GB:", win->theme.muted_text);
            for (int i = 0; i < app->partition_count && i < 4; i++) {
                char lab[16]; char *o = lab;
                uint64_t v = app->sizes_gb[i];
                if (v == 0) { lab[0] = '0'; lab[1] = '\0'; }
                else { char rev[16]; int c = 0; while (v) { rev[c++] = '0' + v % 10; v /= 10; } int p = 0; while (c) lab[p++] = rev[--c]; lab[p] = '\0'; }
                pg_window_text(win, 110 + i * 110, y, lab, win->theme.text);
                char tag[8]; tag[0] = 'P'; tag[1] = '0' + i + 1; tag[2] = '\0';
                pg_window_text(win, 90 + i * 110, y, tag, win->theme.muted_text);
                if (pg_button(win, (struct pg_rect){135 + (uint32_t)i * 110, y - 4, 24, 22}, "-", ev)) if (app->sizes_gb[i] > 0) app->sizes_gb[i]--;
                if (pg_button(win, (struct pg_rect){162 + (uint32_t)i * 110, y - 4, 24, 22}, "+", ev)) app->sizes_gb[i]++;
            }
        }

        y = win->client.height - 52;
        const char *label = app->confirm_format ? "CONFIRM ERASE" : "Format";
        char btn_label[32]; char *bl = append_text(btn_label, label);
        (void)append_text(bl, app->fs_type == FS_TYPE_EXT2 ? " EXT2" : " FAT32");
        if (pg_button(win, (struct pg_rect){20, y, 170, 30}, btn_label, ev)) {
            if (!d->writable || !d->operational || !d->serial[0]) {
                pc_copy(app->status, "Disk cannot be formatted safely.", sizeof(app->status));
                app->confirm_format = false;
            } else if (!app->confirm_format) {
                app->confirm_format = true;
                pc_copy(app->status, "Warning: confirmation erases disk.", sizeof(app->status));
            } else if (app->custom_format && app->partition_count > 1) {
                struct fat32_custom_format_request req = {0};
                pc_copy(req.device, d->name, sizeof(req.device));
                req.partition_count = (uint32_t)app->partition_count;
                for (int i = 0; i < app->partition_count && i < 4; i++) req.sizes_gb[i] = app->sizes_gb[i];
                int32_t r = (int32_t)pc_syscall(SYS_FAT32_FORMAT_CUSTOM, (uint64_t)(uintptr_t)&req, 0, 0);
                app->confirm_format = false;
                if (r == 0) pc_copy(app->status, "Custom FAT32 format done.", sizeof(app->status));
                else { char msg[96]; pc_copy(msg, pf_strerror(r), sizeof(msg)); pc_copy(app->status, msg, sizeof(app->status)); if (r == PF_ERROR_BUSY) pc_copy(app->status, "busy - system disk, pick another", sizeof(app->status)); }
                refresh(app);
            } else {
                int32_t r = pc_format_device_ex(d->name, d->serial, app->fs_type);
                app->confirm_format = false;
                if (r == 0) pc_copy(app->status, app->fs_type == FS_TYPE_EXT2 ? "EXT2 format done." : "FAT32 format done.", sizeof(app->status));
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
    struct disk_app app = {.selected = -1, .fs_type = FS_TYPE_FAT32, .partition_count = 1};
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
