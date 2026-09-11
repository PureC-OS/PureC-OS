#pragma once
#include <stdbool.h>
#include <stdint.h>

enum desktop_app {
    DESKTOP_APP_CLOCK,
    DESKTOP_APP_CALCULATOR,
    DESKTOP_APP_CALENDAR
};

void desktop_apps_init(void);
void desktop_apps_open(
    enum desktop_app app,
    uint32_t screen_width,
    uint32_t screen_height
);
void desktop_apps_draw(void);
void desktop_apps_update(void);
void desktop_apps_save_time(void);
bool desktop_apps_is_visible(void);
bool desktop_apps_contains_point(int32_t x, int32_t y);
bool desktop_apps_handle_mouse(
    int32_t x,
    int32_t y,
    uint8_t buttons,
    bool pressed,
    bool released,
    uint32_t screen_width,
    uint32_t screen_height,
    bool *redraw_required
);
bool desktop_apps_handle_key(char key);
