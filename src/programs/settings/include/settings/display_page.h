#pragma once
#include "../../../libgui/include/puregui.h"
#include "settings/display.h"
void display_page_draw(struct pg_window *window,
                       struct display_settings *display,
                       const struct pg_event *event);
