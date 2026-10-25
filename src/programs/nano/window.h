#pragma once

#include <stdbool.h>
#include "../../libgui/include/puregui.h"

struct nano_window {
    struct pg_window gui;
    uint32_t inset;
    bool render_active;
};

bool nano_window_init(struct nano_window *window);
bool nano_window_begin_render(struct nano_window *window);
void nano_window_end_render(struct nano_window *window);
bool nano_window_poll_event(struct nano_window *window,
                            struct pg_event *event);
bool nano_window_is_minimized(const struct nano_window *window);
uint32_t nano_window_console_rows(const struct nano_window *window);
uint32_t nano_window_console_columns(const struct nano_window *window);
void nano_window_shutdown(struct nano_window *window);
