#pragma once
#include "../../../libgui/include/puregui.h"
#include "settings/personalization.h"
void appearance_page_draw(struct pg_window *window,
                          struct personalization_settings *appearance,
                          const struct pg_event *event);
