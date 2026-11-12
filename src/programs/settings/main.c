#include "../../libgui/include/puregui.h"
#include "../../libc/include/purec.h"
#include "settings/app.h"

static struct settings_app app;
static struct ui ui;
static struct sg_canvas canvas;

static void palette_update(void)
{
    struct pg_theme t = pg_theme_by_name(app.appearance.theme);
    ui_palette_from_theme(&ui.pal, t.window, t.text, t.muted_text, t.accent, t.danger);
}

static void render(struct pg_window *w, uint32_t *px)
{
    int cw = (int)w->client.width, ch = (int)w->client.height;
    w->theme = pg_theme_by_name(app.appearance.theme);
    palette_update();
    sg_init(&canvas, px, cw, ch);
    ui.c = &canvas;
    ui.now = settings_now();
    settings_app_frame(&app, &ui, cw, ch);
    pg_window_begin(w);
    if (!pg_window_is_minimized(w))
        pg_window_blit(w, 0, 0, (uint32_t)cw, (uint32_t)ch, px, (uint32_t)cw);
    pg_window_end(w);
    ui.pressed = false;
    ui.released = false;
    ui.key = 0;
    ui.special = 0;
    ui.wheel = 0;
}

static int settings_main(void)
{
    struct pc_display_info d;
    if (!pc_display_get_info(&d) || !d.available)
        return 1;
    uint32_t width = d.width > 1240 ? 1180 : d.width - 40;
    uint32_t height = d.height > 860 ? 800 : d.height - 60;
    if (width < 720 || height < 480)
        return 1;
    uint32_t *px = pc_heap_grow((uint64_t)d.width * d.height * 4);
    if (!px)
        return 1;

    settings_app_init(&app);
    struct pg_window w;
    if (!pg_window_center(&w, "Settings", width, height))
        return 1;

    bool dirty = true;
    uint64_t last_frame = 0, last_live = 0;
    while (pg_window_is_open(&w)) {
        struct pg_event ev;
        int handled = 0;
        bool input = false;
        while (handled < 64 && pg_window_poll_event(&w, &ev)) {
            handled++;
            if (ev.type == PG_EVENT_CLOSE)
                return 0;
            int mx = ev.x - (int32_t)w.client.x, my = ev.y - (int32_t)w.client.y;
            switch (ev.type) {
            case PG_EVENT_MOUSE_MOVE:
                ui.mx = mx;
                ui.my = my;
                dirty = true;
                break;
            case PG_EVENT_MOUSE_DOWN:
                ui.mx = mx;
                ui.my = my;
                if (ev.button == 1) {
                    ui.down = true;
                    ui.pressed = true;
                    input = true;
                }
                dirty = true;
                break;
            case PG_EVENT_MOUSE_UP:
                ui.mx = mx;
                ui.my = my;
                if (ev.button == 1) {
                    ui.down = false;
                    ui.released = true;
                    input = true;
                }
                dirty = true;
                break;
            case PG_EVENT_KEY:
                ui.key = ev.key;
                input = true;
                dirty = true;
                break;
            case PG_EVENT_SPECIAL_KEY:
                ui.special = ev.key;
                input = true;
                dirty = true;
                break;
            default:
                dirty = true;
                break;
            }
            if (input)
                break;
        }
        int32_t wheel = pg_window_take_wheel(&w);
        if (wheel) {
            ui.wheel += wheel;
            dirty = true;
        }
        uint64_t now = settings_now();
        if (settings_page_wants_live(app.page) && now - last_live >= 1000) {
            last_live = now;
            dirty = true;
        }
        if (ui.animating && now - last_frame >= 250)
            dirty = true;
        if (!dirty) {
            pc_sleep(16);
            continue;
        }
        dirty = false;
        last_frame = now;
        render(&w, px);
        if (input)
            render(&w, px);
    }
    return 0;
}

void _start(void) { pc_exit(settings_main()); }
