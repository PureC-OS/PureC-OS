#pragma once
#include "../../../libgui/include/puregui.h"
#include "settings/model.h"
#include "settings/personalization.h"
#include "settings/display.h"
#include "settings/ui.h"

enum settings_page {
    PAGE_ABOUT,
    PAGE_NETWORK,
    PAGE_SOUND,
    PAGE_DISPLAY,
    PAGE_APPEARANCE,
    PAGE_STORAGE,
    PAGE_DATETIME,
    PAGE_POWER,
    PAGE_COUNT
};

enum settings_confirm { CONFIRM_NONE, CONFIRM_SHUTDOWN, CONFIRM_REBOOT };

struct settings_app {
    struct settings_model model;
    struct personalization_settings appearance;
    struct display_settings display;
    int page;
    int scroll[PAGE_COUNT];
    int content_h[PAGE_COUNT];
    int confirm;
    char toast[96];
    uint32_t toast_color;
    uint64_t toast_until;
    uint64_t last_refresh;
    bool refresh;
};

struct page_ctx {
    struct settings_app *app;
    struct ui *u;
    int x, y, w;
    int view_h;
};

void settings_app_init(struct settings_app *app);
void settings_app_frame(struct settings_app *app, struct ui *u, int w, int h);
void settings_toast(struct settings_app *app, const char *text, uint32_t color);
bool settings_page_wants_live(int page);

int page_about(struct page_ctx *p);
int page_network(struct page_ctx *p);
int page_sound(struct page_ctx *p);
int page_display(struct page_ctx *p);
int page_appearance(struct page_ctx *p);
int page_storage(struct page_ctx *p);
int page_datetime(struct page_ctx *p);
int page_power(struct page_ctx *p);
