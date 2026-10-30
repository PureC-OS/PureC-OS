#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "../kernel/syscall/syscall.h"

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
uint32_t window_manager_focused_pid(void);
bool window_manager_focus_pid(uint32_t pid);
uint32_t window_manager_list(uint32_t *pids,
                             struct gui_window_request *frames,
                             uint32_t capacity);
void window_manager_set_suspended(bool suspended);
void window_manager_request_repaint(uint32_t excluded_pid);
bool window_manager_repaint_pending(void);
void window_manager_cancel_repaint(void);

#define WM_TITLEBAR_HEIGHT 26
#define WM_BORDER 2
#define WM_BUTTON_SIZE 14
#define WM_BUTTON_Y 8
#define WM_CLOSE_OFFSET 24
#define WM_MINIMIZE_OFFSET 44
#define WM_MAXIMIZE_OFFSET 64

void window_manager_set_server_mode(bool enabled);
bool window_manager_server_mode(void);
void window_manager_set_work_area(int32_t x, int32_t y, uint32_t width,
                                  uint32_t height);
uint32_t window_manager_pointer(int32_t x, int32_t y, uint8_t buttons,
                                bool *focus_changed);
bool window_manager_info(uint32_t pid, struct gui_window_info *out);
bool window_manager_set_flags(uint32_t pid, uint32_t flags);
bool window_manager_command(uint32_t pid, uint32_t command);
uint32_t window_manager_tasks(struct gui_task_info *out, uint32_t capacity);
bool window_manager_activate(uint32_t pid);
