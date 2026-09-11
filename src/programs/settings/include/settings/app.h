#pragma once
#include "../../../libgui/include/puregui.h"
#include "settings/model.h"
#include "settings/personalization.h"
struct settings_app {
    struct settings_model model;
    struct personalization_settings appearance;
    int tab;
};
void settings_app_init(struct settings_app *app);
void settings_app_draw(struct settings_app *app, struct pg_window *w, const struct pg_event *ev);
