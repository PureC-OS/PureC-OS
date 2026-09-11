#pragma once

#include "puregui.h"

enum pg_widget_state {
    PG_WIDGET_NORMAL=0,
    PG_WIDGET_HOVER,
    PG_WIDGET_PRESSED,
    PG_WIDGET_DISABLED
};

void pg_label(struct pg_window *window, uint32_t x, uint32_t y,
              const char *text);
void pg_panel(struct pg_window *window, struct pg_rect bounds);
bool pg_button(struct pg_window *window, struct pg_rect bounds,
               const char *label, const struct pg_event *event);
