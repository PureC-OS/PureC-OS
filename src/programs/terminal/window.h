#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "../../libgui/include/puregui.h"

struct terminal_window {
    struct pg_window gui;
    uint32_t inset;
};

bool terminal_window_init(struct terminal_window *terminal);
bool terminal_window_init_titled(struct terminal_window *terminal,
                                 const char *title);
bool terminal_window_repaint(struct terminal_window *terminal);
bool terminal_window_restore(struct terminal_window *terminal);
bool terminal_window_service(struct terminal_window *terminal);
bool terminal_window_read_line(struct terminal_window *terminal,
                               const char *prompt,
                               char *buffer, uint32_t capacity);
void terminal_window_shutdown(struct terminal_window *terminal);
