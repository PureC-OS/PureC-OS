#pragma once

#include <stdbool.h>
#include <stdint.h>

#define BOTTOM_PANEL_HEIGHT 30
#define TOP_PANEL_HEIGHT 28
#define TOP_POWER_W 30

enum bottom_panel_action {
    BOTTOM_ACTION_NONE = 0,
    BOTTOM_ACTION_EXEC,
    BOTTOM_ACTION_BUILTIN,
    BOTTOM_ACTION_REBOOT,
    BOTTOM_ACTION_SHUTDOWN
};

void bottom_panel_init(void);
void bottom_panel_draw(uint32_t screen_width, uint32_t screen_height);
bool bottom_panel_handle_mouse(int32_t x, int32_t y, uint8_t buttons,
                               bool pressed, bool released,
                               uint32_t screen_width, uint32_t screen_height,
                               bool *redraw_required);
bool bottom_panel_is_menu_open(void);
void bottom_panel_close_menu(void);
bool bottom_panel_take_action(enum bottom_panel_action *kind,
                              char *path, uint32_t path_cap,
                              char *builtin, uint32_t builtin_cap);
bool bottom_panel_needs_redraw(void);
uint32_t bottom_panel_height(void);
void top_panel_draw(uint32_t screen_width, const char *focused_title);
void top_panel_power_rect(uint32_t screen_width, uint32_t *x, uint32_t *y,
                          uint32_t *w, uint32_t *h);
uint32_t panel_face_color(void);
uint32_t panel_text_color(bool muted);
void panel_bevel_button(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                        bool pressed);
void top_panel_draw_power_menu(uint32_t screen_width);
