#include "../../libgui/include/puregui.h"
#include "../../libc/include/purec.h"
#include "settings/app.h"

static struct settings_app app;
static struct pg_ui ui;

static void render(struct pg_window *w)
{
    w->theme = pg_theme_by_name(app.appearance.theme);
    pgu_palette_from_theme(&ui.pal, &w->theme);
    pg_window_begin(w);
    if (!pg_window_is_minimized(w)) {
        struct pg_canvas *c = pg_window_canvas(w);
        if (c) {
            pgu_frame_begin(&ui, c, settings_now());
            settings_app_frame(&app, &ui, c->w, c->h);
            pgu_frame_end(&ui);
        }
    }
    pg_window_end(w);
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
            pgu_feed(&ui, &w, &ev);
            dirty = true;
            if (ev.type == PG_EVENT_MOUSE_DOWN || ev.type == PG_EVENT_MOUSE_UP || ev.type == PG_EVENT_KEY ||
                ev.type == PG_EVENT_SPECIAL_KEY) {
                input = true;
                break;
            }
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
        render(&w);
        if (input)
            render(&w);
    }
    return 0;
}

void _start(void) { pc_exit(settings_main()); }
