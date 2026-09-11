#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "../kernel/syscall/syscall.h"

#define WINDOW_MANAGER_CAPACITY 8

bool window_manager_register(uint32_t pid,
                             const struct gui_window_request *request);
bool window_manager_update(uint32_t pid,
                           const struct gui_window_request *request);
void window_manager_unregister(uint32_t pid);
uint32_t window_manager_state(uint32_t pid);
void window_manager_finish_repaint(uint32_t pid);
bool window_manager_handle_pointer(int32_t x, int32_t y, bool pressed,
                                   bool *focus_changed);
bool window_manager_has_focus(void);
void window_manager_set_suspended(bool suspended);
void window_manager_request_repaint(uint32_t excluded_pid);
bool window_manager_repaint_pending(void);
void window_manager_cancel_repaint(void);
