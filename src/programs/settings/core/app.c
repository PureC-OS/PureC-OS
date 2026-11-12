#include "settings/app.h"
#include "settings/fmt.h"
#include "../../../libc/include/purec.h"

#define SIDEBAR_W 252
#define HEADER_H 92

struct nav_item {
    int page;
    int icon;
    const char *label;
    const char *title;
    const char *subtitle;
};

static const struct nav_item nav[PAGE_COUNT] = {
    {PAGE_ABOUT, SI_INFORMATION_OUTLINE, "About", "About this PC", "Hardware, memory and system overview"},
    {PAGE_NETWORK, SI_WIFI, "Network", "Network & Internet", "Wi-Fi, wired adapters and connectivity"},
    {PAGE_SOUND, SI_VOLUME_HIGH, "Sound", "Sound", "Volume, output device and audio test"},
    {PAGE_DISPLAY, SI_MONITOR, "Display", "Display", "Resolution and interface scale"},
    {PAGE_APPEARANCE, SI_PALETTE, "Appearance", "Appearance", "Theme, wallpaper and fonts"},
    {PAGE_STORAGE, SI_HARDDISK, "Storage", "Storage", "Disks, file system and free space"},
    {PAGE_DATETIME, SI_CLOCK_OUTLINE, "Date & time", "Date & time", "Clock, time zone and uptime"},
    {PAGE_POWER, SI_POWER, "Power", "Power & battery", "Battery status, restart and shut down"},
};

static const char *group_of(int index)
{
    if (index == 0)
        return "SYSTEM";
    if (index == 4)
        return "PERSONAL";
    if (index == 5)
        return "DEVICE";
    return 0;
}

void settings_app_init(struct settings_app *app)
{
    for (unsigned i = 0; i < sizeof(*app); i++)
        ((char *)app)[i] = 0;
    settings_model_init(&app->model);
    (void)settings_model_load(&app->model);
    (void)settings_model_apply(&app->model);
    appearance_defaults(&app->appearance);
    (void)appearance_load(&app->appearance);
    display_defaults(&app->display);
    (void)display_load(&app->display);
    app->page = PAGE_ABOUT;
    app->refresh = true;
}

void settings_toast(struct settings_app *app, const char *text, uint32_t color)
{
    fmt_copy(app->toast, text, sizeof(app->toast));
    app->toast_color = color;
    app->toast_until = pc_time_monotonic_ms() + 2600;
}

bool settings_page_wants_live(int page)
{
    return page == PAGE_ABOUT || page == PAGE_NETWORK || page == PAGE_DATETIME ||
           page == PAGE_POWER || page == PAGE_SOUND;
}

static void draw_sidebar(struct settings_app *app, struct ui *u, int h)
{
    const struct ui_palette *p = &u->pal;
    struct sg_canvas *c = u->c;
    sg_fill(c, 0, 0, SIDEBAR_W, h, p->sidebar);
    sg_fill_a(c, SIDEBAR_W - 1, 0, 1, h, p->border, 150);

    sg_rrect_grad(c, 20, 22, 40, 40, 12, sg_lighten(p->accent, 30), sg_mix(p->accent, 0x5b21b6, 110),
                  false);
    sg_icon(c, SI_COG, SG_ICON_M, 26, 28, 0xffffff);
    sg_text(c, SF_HEAD, 72, 22, "Settings", p->text);
    sg_text(c, SF_SMALL, 72, 43, "PureC OS 1.2.29", p->text3);

    struct user_info user;
    char name[48];
    struct fmt f;
    fmt_init(&f, name, sizeof(name));
    if (pc_user_lookup(0, (uint32_t)pc_getuid(), &user) == 0 && user.name[0])
        fs(&f, user.name);
    else
        fs(&f, "root");
    int cy = 80;
    sg_rrect(c, 14, cy, SIDEBAR_W - 28, 52, 12, p->surface, p->light ? 255 : 200);
    sg_circle(c, 40, cy + 26, 16, sg_mix(p->accent, p->surface, 120), 255);
    char initial[2] = {name[0] >= 'a' && name[0] <= 'z' ? (char)(name[0] - 32) : name[0], 0};
    sg_text_center(c, SF_HEAD, 24, cy + 15, 32, initial, p->on_accent);
    sg_text(c, SF_MEDIUM, 66, cy + 9, name, p->text);
    sg_text(c, SF_SMALL, 66, cy + 28, pc_getuid() == 0 ? "Administrator" : "Standard user", p->text3);

    int y = cy + 70;
    for (int i = 0; i < PAGE_COUNT; i++) {
        const char *g = group_of(i);
        if (g) {
            if (i)
                y += 8;
            sg_text(c, SF_SMALL, 26, y, g, p->text3);
            y += 24;
        }
        const struct nav_item *it = &nav[i];
        bool sel = app->page == it->page;
        uint32_t id = ui_id("nav", i);
        if (ui_click(u, id, 12, y, SIDEBAR_W - 24, 40) && !sel) {
            app->page = it->page;
            app->refresh = true;
            u->focus = 0;
        }
        bool hot = u->hot == id;
        if (sel) {
            sg_rrect(c, 12, y, SIDEBAR_W - 24, 40, 10, p->accent, p->light ? 34 : 44);
            sg_rrect(c, 12, y + 10, 4, 20, 2, p->accent, 255);
        } else if (hot) {
            sg_rrect(c, 12, y, SIDEBAR_W - 24, 40, 10, p->surface2, 255);
        }
        sg_icon(c, it->icon, SG_ICON_S, 30, y + 11, sel ? p->accent : p->text2);
        sg_text(c, sel ? SF_MEDIUM : SF_BODY, 60, y + 11, it->label, sel ? p->text : p->text2);
        y += 44;
    }
}

static void draw_toast(struct settings_app *app, struct ui *u, int x0, int w, int h)
{
    if (!app->toast[0] || u->now >= app->toast_until)
        return;
    int tw = sg_text_w(SF_MEDIUM, app->toast) + 56;
    int x = x0 + (w - tw) / 2, y = h - 64;
    sg_shadow(u->c, x, y, tw, 40, 20, 8, 90);
    sg_rrect(u->c, x, y, tw, 40, 20, u->pal.light ? 0x1e1e2e : sg_lighten(u->pal.surface3, 10), 255);
    sg_circle(u->c, x + 22, y + 20, 5, app->toast_color, 255);
    sg_text(u->c, SF_MEDIUM, x + 36, y + 11, app->toast, u->pal.light ? 0xffffff : u->pal.text);
    u->animating = true;
}

static void draw_confirm(struct settings_app *app, struct ui *u, int w, int h)
{
    if (app->confirm == CONFIRM_NONE)
        return;
    const struct ui_palette *p = &u->pal;
    sg_fill_a(u->c, 0, 0, w, h, 0x000000, 140);
    int mw = 420, mh = 210;
    int x = (w - mw) / 2, y = (h - mh) / 2;
    sg_shadow(u->c, x, y, mw, mh, 16, 14, 140);
    sg_rrect(u->c, x, y, mw, mh, 16, p->light ? 0xffffff : p->surface, 255);
    sg_rrect_stroke(u->c, x, y, mw, mh, 16, p->border, 160);
    bool off = app->confirm == CONFIRM_SHUTDOWN;
    ui_icon_tile(u, x + 24, y + 24, 44, off ? SI_POWER : SI_RESTART, off ? p->danger : p->accent);
    sg_text(u->c, SF_TITLE, x + 84, y + 26, off ? "Shut down?" : "Restart?", p->text);
    sg_text(u->c, SF_BODY, x + 24, y + 88,
            off ? "The computer will power off now." : "The computer will restart now.", p->text2);
    sg_text(u->c, SF_BODY, x + 24, y + 110, "Unsaved work in open apps will be lost.", p->text3);
    u->modal = false;
    if (ui_button(u, ui_id("confirm-cancel", 0), x + mw - 252, y + mh - 60, 108, 38, "Cancel", -1,
                  UI_BTN_SECONDARY) ||
        u->key == 27) {
        app->confirm = CONFIRM_NONE;
        u->key = 0;
    }
    if (ui_button(u, ui_id("confirm-ok", 0), x + mw - 134, y + mh - 60, 110, 38,
                  off ? "Shut down" : "Restart", -1, off ? UI_BTN_DANGER : UI_BTN_PRIMARY)) {
        int what = app->confirm;
        app->confirm = CONFIRM_NONE;
        if (what == CONFIRM_SHUTDOWN)
            pc_shutdown();
        else
            pc_reboot();
    }
}

static int (*const pages[PAGE_COUNT])(struct page_ctx *) = {
    page_about, page_network, page_sound, page_display,
    page_appearance, page_storage, page_datetime, page_power,
};

void settings_app_frame(struct settings_app *app, struct ui *u, int w, int h)
{
    const struct ui_palette *p = &u->pal;
    struct sg_canvas *c = u->c;
    u->hot = 0;
    u->any_hot = false;
    u->animating = false;
    u->modal = app->confirm != CONFIRM_NONE;

    sg_fill(c, 0, 0, w, h, p->bg);
    draw_sidebar(app, u, h);

    int x0 = SIDEBAR_W;
    int cw = w - x0;
    int pad = cw > 900 ? 44 : 30;
    int content_w = cw - 2 * pad;
    if (content_w > 860)
        content_w = 860;
    int cx = x0 + (cw - content_w) / 2;
    if (cx < x0 + pad)
        cx = x0 + pad;

    const struct nav_item *it = &nav[app->page];
    sg_text(c, SF_TITLE, cx, 26, it->title, p->text);
    sg_text(c, SF_BODY, cx, 58, it->subtitle, p->text3);

    int view_y = HEADER_H;
    int view_h = h - view_y;
    int *scroll = &app->scroll[app->page];
    if (u->wheel && !u->modal && u->mx >= x0) {
        *scroll += u->wheel * 48;
        u->wheel = 0;
    }
    if (!u->modal && u->special == 6)
        *scroll -= view_h - 60;
    if (!u->modal && u->special == 7)
        *scroll += view_h - 60;
    int max_scroll = app->content_h[app->page] + 40 - view_h;
    if (max_scroll < 0)
        max_scroll = 0;
    if (*scroll > max_scroll)
        *scroll = max_scroll;
    if (*scroll < 0)
        *scroll = 0;

    if (*scroll > 0)
        sg_fill_a(c, x0, view_y - 1, cw, 1, p->border, 160);

    sg_clip(c, x0, view_y, cw, view_h);
    struct page_ctx ctx = {app, u, cx, view_y + 8 - *scroll, content_w, view_h};
    bool saved_modal = u->modal;
    if (u->my < view_y)
        u->modal = true;
    int ch = pages[app->page](&ctx);
    u->modal = saved_modal;
    app->content_h[app->page] = ch;
    sg_unclip(c);

    if (max_scroll > 0) {
        int track_h = view_h - 16;
        int thumb_h = track_h * view_h / (ch + 40);
        if (thumb_h < 40)
            thumb_h = 40;
        int thumb_y = view_y + 8 + (track_h - thumb_h) * *scroll / max_scroll;
        sg_rrect(c, w - 10, thumb_y, 5, thumb_h, 3, p->text3, 110);
    }

    draw_toast(app, u, x0, cw, h);
    draw_confirm(app, u, w, h);
    app->refresh = false;
}
