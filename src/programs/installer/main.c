#include "../../libgui/include/puregui.h"
#include "../../libfs/include/purefs.h"
#include "../../libc/include/purec.h"
#include "app.h"
#include "progress.h"

#define INSTALL_IDLE 0
#define INSTALL_RUNNING 1
#define INSTALL_COMPLETE 2
#define INSTALL_FAILED 3

#define CARD_MAX_W 1100
#define CARD_MAX_H 700
#define CANVAS_MARGIN 32
#define BAND_ROWS 16
#define KEY_QUEUE 16

static struct pg_window win;
static struct app app;
static struct ui ui;
static uint32_t *canvas_px;
static uint32_t *bg_px;
static uint32_t *shown_px;
static uint32_t *band_px;
static int32_t cv_w, cv_h, cv_x, cv_y;
static bool last_closable = true;

static bool contains(const char *hay, const char *needle) {
    for (; *hay; hay++) {
        const char *h = hay, *n = needle;
        while (*h && *n && *h == *n)
            h++, n++;
        if (!*n)
            return true;
    }
    return false;
}

static int32_t phase_of(const char *stage) {
    if (contains(stage, "Validating") || contains(stage, "GPT"))
        return 0;
    if (contains(stage, "EFI"))
        return 1;
    if (contains(stage, "Formatting"))
        return 2;
    if (contains(stage, "Copying") || contains(stage, "firmware"))
        return 3;
    if (contains(stage, "account"))
        return 4;
    if (contains(stage, "mounted") || contains(stage, "configuration"))
        return 5;
    return -1;
}

static bool write_install_config(void) {
    static const char directory[] = "/purec";
    static const char path[] = "/purec/install.cfg";
    static const char config[] =
        "boot=/bin/init\ninstaller=gui-ring3\nfilesystem=ext2\n";
    (void)pf_create_dir(directory);
    (void)pf_create_file(path);
    return pf_write_file(path, config, sizeof(config) - 1) >= 0;
}

static void set_closable(bool closable) {
    if (closable != last_closable) {
        pg_window_set_closable(&win, closable);
        last_closable = closable;
    }
}

static void blit_rows(int32_t y0, int32_t y1) {
    if (y1 <= y0)
        return;
    (void)pg_window_blit(&win, cv_x, cv_y + y0, (uint32_t)cv_w,
                         (uint32_t)(y1 - y0), canvas_px + (int64_t)y0 * cv_w,
                         (uint32_t)cv_w);
    for (int32_t y = y0; y < y1; y++)
        for (int32_t x = 0; x < cv_w; x++)
            shown_px[(int64_t)y * cv_w + x] = canvas_px[(int64_t)y * cv_w + x];
}

static void render(bool force) {
    for (int64_t i = 0; i < (int64_t)cv_w * cv_h; i++)
        canvas_px[i] = bg_px[i];
    ui.frame++;
    view_frame(&app, &ui);
    int32_t first = -1, last = -1;
    for (int32_t y = 0; y < cv_h; y++) {
        const uint32_t *a = canvas_px + (int64_t)y * cv_w;
        const uint32_t *b = shown_px + (int64_t)y * cv_w;
        bool same = true;
        for (int32_t x = 0; x < cv_w; x++)
            if (a[x] != b[x]) {
                same = false;
                break;
            }
        if (!same) {
            if (first < 0)
                first = y;
            last = y;
        }
    }
    if (force) {
        first = 0;
        last = cv_h - 1;
    }
    if (first >= 0)
        blit_rows(first, last + 1);
}

static void paint_full(void) {
    int32_t cw = (int32_t)win.client.width, ch = (int32_t)win.client.height;
    pg_window_begin(&win);
    struct ui_canvas band = {band_px, cw, BAND_ROWS};
    for (int32_t y = 0; y < ch; y += BAND_ROWS) {
        band.h = ch - y < BAND_ROWS ? ch - y : BAND_ROWS;
        ui_background(&band, 0, y, cw, ch);
        (void)pg_window_blit(&win, 0, y, (uint32_t)cw, (uint32_t)band.h,
                             band_px, (uint32_t)cw);
    }
    render(true);
    pg_window_end(&win);
}

static bool alloc_buffers(void) {
    int32_t cw = (int32_t)win.client.width, ch = (int32_t)win.client.height;
    cv_w = cw < CARD_MAX_W + 2 * CANVAS_MARGIN ? cw
                                               : CARD_MAX_W + 2 * CANVAS_MARGIN;
    cv_h = ch < CARD_MAX_H + 2 * CANVAS_MARGIN ? ch
                                               : CARD_MAX_H + 2 * CANVAS_MARGIN;
    cv_x = (cw - cv_w) / 2;
    cv_y = (ch - cv_h) / 2;
    uint64_t px = (uint64_t)cv_w * (uint64_t)cv_h * 4;
    uint64_t band = (uint64_t)cw * BAND_ROWS * 4;
    uint8_t *mem = (uint8_t *)pc_heap_grow(px * 3 + band);
    if (!mem)
        return false;
    canvas_px = (uint32_t *)mem;
    bg_px = (uint32_t *)(mem + px);
    shown_px = (uint32_t *)(mem + px * 2);
    band_px = (uint32_t *)(mem + px * 3);
    for (uint64_t i = 0; i < px / 4; i++)
        shown_px[i] = 0xFFFFFFFFu;
    struct ui_canvas bg = {bg_px, cv_w, cv_h};
    view_background(&bg, cv_x, cv_y, cw, ch);
    ui.c.px = canvas_px;
    ui.c.w = cv_w;
    ui.c.h = cv_h;
    return true;
}

static bool start_installation(void) {
    const struct storage_device_info *disk = &app.disks[app.selected];
    struct install_account_request accounts;
    if (app.same_root) {
        pc_copy(app.s.root_password, app.s.user_password,
                sizeof(app.s.root_password));
    }
    bool built = setup_build_request(&app.s, &accounts);
    setup_wipe(&app.s);
    for (uint32_t i = 0; i < sizeof(app.user_pass2); i++)
        ((volatile char *)app.user_pass2)[i] = 0;
    for (uint32_t i = 0; i < sizeof(app.root_pass2); i++)
        ((volatile char *)app.root_pass2)[i] = 0;
    if (!built) {
        app.fail_code = PF_ERROR_INVALID;
        app.fail_text = "could not prepare the user accounts";
        return false;
    }
    int32_t result = pc_install_start_accounts(disk->name, disk->serial,
                                               FS_TYPE_EXT2, &accounts);
    for (uint32_t i = 0; i < sizeof(accounts); i++)
        ((volatile uint8_t *)&accounts)[i] = 0;
    if (result < 0) {
        app.fail_code = result;
        app.fail_text = installer_progress_error_text(result);
        return false;
    }
    return true;
}

static void fail(int32_t code, const char *text) {
    app.fail_code = code;
    app.fail_text = text;
    app.page = PAGE_FAILED;
    app.install_started = false;
}

static void poll_install(void) {
    struct install_status status;
    if (!pc_install_status(&status)) {
        fail(PF_ERROR_IO, "cannot read the installation status");
        return;
    }
    if (status.state == INSTALL_FAILED) {
        fail(status.result, installer_progress_error_text(status.result));
        return;
    }
    uint32_t p = status.progress > 100 ? 100 : status.progress;
    if (p * 10 > app.progress_target)
        app.progress_target = p * 10;
    if (status.stage[0]) {
        pc_copy(app.stage, status.stage, sizeof(app.stage));
        int32_t ph = phase_of(status.stage);
        if (ph > app.phase)
            app.phase = ph;
    }
    if (status.state == INSTALL_COMPLETE) {
        pc_copy(app.stage, "Writing the installation configuration",
                sizeof(app.stage));
        if (!write_install_config()) {
            fail(PF_ERROR_IO, "cannot write /purec/install.cfg");
            return;
        }
        app.progress_target = 1000;
        app.progress_shown = 1000;
        app.phase = APP_PHASES;
        app.page = PAGE_DONE;
    }
}

static void animate_progress(void) {
    if (app.progress_shown < app.progress_target) {
        uint32_t step = (app.progress_target - app.progress_shown) / 6;
        app.progress_shown += step ? step : 1;
        if (app.progress_shown > app.progress_target)
            app.progress_shown = app.progress_target;
    }
}

static int installer_main(void) {
    struct pc_display_info info;
    if (!pc_display_get_info(&info) || !info.width || !info.height) {
        pc_write("installer: no display available\n");
        return 1;
    }
    if (!pg_window_init(&win, "PureC OS - Setup", 0, 0, info.width,
                        info.height)) {
        pc_write("installer: cannot open the setup window\n");
        return 1;
    }
    pg_window_set_esc_closes(&win, false);
    if (!alloc_buffers()) {
        pc_write("installer: out of memory\n");
        pg_window_close(&win);
        return 1;
    }

    app.disk_count = pc_list_disks(app.disks, APP_MAX_DISKS);
    if (app.disk_count < 0)
        app.disk_count = 0;
    view_init(&app);
    ui.focus = -1;
    ui.active = -1;
    ui.in.key = -1;
    ui.in.special = -1;

    struct install_status status;
    if (pc_install_status(&status) && status.state != INSTALL_IDLE &&
        status.state != INSTALL_FAILED) {
        app.resumed = true;
        app.page = PAGE_INSTALL;
        app.install_started = true;
        app.start_install = false;
    }

    paint_full();
    uint32_t tick = 0;
    bool dirty = false;
    int32_t keys[KEY_QUEUE];
    int32_t specials[KEY_QUEUE];
    uint32_t key_count = 0;
    while (pg_window_is_open(&win)) {
        if (win.frame.x != 0 || win.frame.y != 0)
            (void)pg_window_move(&win, 0, 0);
        key_count = 0;
        bool repaint = false;
        bool pointer = false;
        struct pg_event ev;
        int32_t budget = 64;
        while (budget-- > 0 && pg_window_poll_event(&win, &ev)) {
            switch (ev.type) {
            case PG_EVENT_CLOSE:
                return 0;
            case PG_EVENT_REPAINT:
            case PG_EVENT_MOVE:
            case PG_EVENT_MINIMIZE:
                repaint = true;
                break;
            case PG_EVENT_KEY:
                if (key_count < KEY_QUEUE) {
                    keys[key_count] = ev.key;
                    specials[key_count++] = -1;
                }
                break;
            case PG_EVENT_SPECIAL_KEY:
                if (key_count < KEY_QUEUE) {
                    keys[key_count] = -1;
                    specials[key_count++] = ev.key;
                }
                break;
            case PG_EVENT_MOUSE_DOWN:
                ui.in.mx = ev.x - (int32_t)win.client.x - cv_x;
                ui.in.my = ev.y - (int32_t)win.client.y - cv_y;
                if (ev.button == 1) {
                    ui.in.down = true;
                    ui.in.pressed = true;
                }
                pointer = true;
                break;
            case PG_EVENT_MOUSE_UP:
                ui.in.mx = ev.x - (int32_t)win.client.x - cv_x;
                ui.in.my = ev.y - (int32_t)win.client.y - cv_y;
                if (ev.button == 1) {
                    ui.in.down = false;
                    ui.in.released = true;
                }
                pointer = true;
                break;
            case PG_EVENT_MOUSE_MOVE:
                ui.in.mx = ev.x - (int32_t)win.client.x - cv_x;
                ui.in.my = ev.y - (int32_t)win.client.y - cv_y;
                pointer = true;
                break;
            default:
                break;
            }
        }
        if (pg_window_is_minimized(&win)) {
            pc_sleep(40);
            continue;
        }
        int32_t wheel = pg_window_take_wheel(&win);
        if (wheel) {
            ui.in.wheel = wheel;
            pointer = true;
        }

        tick++;
        bool caret = ((tick / 30) & 1) == 0;
        if (caret != ui.caret_on) {
            ui.caret_on = caret;
            if (app.page == PAGE_COMPUTER || app.page == PAGE_ACCOUNT)
                dirty = true;
        }
        if (app.page == PAGE_TIME) {
            struct time_info now;
            static int64_t shown_second = -1;
            if (pc_time_get(&now) == 0 && now.unix_seconds != shown_second) {
                shown_second = now.unix_seconds;
                dirty = true;
            }
        }

        if (app.page == PAGE_INSTALL) {
            set_closable(false);
            if (app.start_install) {
                app.start_install = false;
                pc_copy(app.stage, "Preparing user accounts", sizeof(app.stage));
                app.phase = 0;
                app.install_started = true;
                ui.in.key = -1;
                ui.in.special = -1;
                render(false);
                if (!start_installation())
                    fail(app.fail_code, app.fail_text);
            } else if (app.install_started && tick % 6 == 0) {
                poll_install();
            }
            animate_progress();
            if (tick % 3 == 0)
                dirty = true;
        } else {
            set_closable(true);
        }

        bool held_pressed = ui.in.pressed, held_released = ui.in.released;
        int32_t held_wheel = ui.in.wheel;
        ui.in.pressed = false;
        ui.in.released = false;
        ui.in.wheel = 0;
        for (uint32_t i = 0; i < key_count; i++) {
            ui.in.key = keys[i];
            ui.in.special = specials[i];
            enum app_page before = app.page;
            view_frame(&app, &ui);
            ui.in.key = -1;
            ui.in.special = -1;
            dirty = true;
            if (app.page != before)
                break;
        }
        ui.in.pressed = held_pressed;
        ui.in.released = held_released;
        ui.in.wheel = held_wheel;
        if (repaint) {
            paint_full();
            dirty = false;
        } else if (pointer || dirty || key_count) {
            render(false);
            ui.in.pressed = false;
            ui.in.released = false;
            ui.in.wheel = 0;
            dirty = false;
            if (app.page != PAGE_INSTALL && ui.dirty) {
                ui.dirty = false;
                render(false);
            }
        }
        if (app.do_reboot) {
            pc_reboot();
            app.do_reboot = false;
        }
        if (app.quit)
            return 0;
        pc_sleep(16);
    }
    return 0;
}

void _start(void) {
    int status = installer_main();
    if (pg_window_is_open(&win))
        pg_window_close(&win);
    pc_exit(status);
}
