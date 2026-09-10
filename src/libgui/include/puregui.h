#pragma once

#include <stdbool.h>
#include <stdint.h>

#define PG_API_VERSION 3
#define PG_VERSION "V1.0.2"
#define PG_VERSION_MAJOR 1
#define PG_VERSION_MINOR 0
#define PG_VERSION_PATCH 2
#define PG_TITLE_CAPACITY 64
#define PG_TITLEBAR_HEIGHT 26
#define PG_WINDOW_BORDER 2

struct pg_rect {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
};

struct pg_theme {
    uint32_t desktop;
    uint32_t window;
    uint32_t titlebar;
    uint32_t border;
    uint32_t text;
    uint32_t muted_text;
    uint32_t accent;
    uint32_t danger;
    uint32_t shadow;
};

enum pg_event_type {
    PG_EVENT_NONE=0,
    PG_EVENT_KEY,
    PG_EVENT_MOUSE_MOVE,
    PG_EVENT_MOUSE_DOWN,
    PG_EVENT_MOUSE_UP,
    PG_EVENT_MOVE,
    PG_EVENT_MINIMIZE,
    PG_EVENT_FOCUS,
    PG_EVENT_REPAINT,
    PG_EVENT_SPECIAL_KEY,
    PG_EVENT_CLOSE
};

struct pg_event {
    enum pg_event_type type;
    int32_t x;
    int32_t y;
    int32_t key;
    uint8_t button;
};

struct pg_window {
    struct pg_rect frame;
    struct pg_rect client;
    struct pg_theme theme;
    char title[PG_TITLE_CAPACITY];
    int32_t previous_mouse_x;
    int32_t previous_mouse_y;
    int32_t drag_offset_x;
    int32_t drag_offset_y;
    uint8_t previous_mouse_buttons;
    bool dragging;
    bool minimized;
    bool open;
    bool focused;
    bool registered;
    bool repainting;
    /* Gate windows (login screen): library must not auto-close on
     * X/Esc nor auto-minimize; default true, change via setter. */
    bool closable;
};

const char *pg_version(void);
struct pg_theme pg_theme_default(void);
bool pg_window_init(struct pg_window *window, const char *title,
                    uint32_t x, uint32_t y,
                    uint32_t width, uint32_t height);
bool pg_window_center(struct pg_window *window, const char *title,
                      uint32_t width, uint32_t height);
void pg_window_begin(struct pg_window *window);
void pg_window_end(struct pg_window *window);
void pg_window_close(struct pg_window *window);
void pg_window_set_closable(struct pg_window *window, bool closable);
bool pg_window_move(struct pg_window *window, uint32_t x, uint32_t y);
void pg_window_minimize(struct pg_window *window);
void pg_window_restore(struct pg_window *window);
bool pg_window_is_open(const struct pg_window *window);
bool pg_window_is_minimized(const struct pg_window *window);
struct pg_rect pg_window_client(const struct pg_window *window);
void pg_window_clear(struct pg_window *window, uint32_t color);
void pg_window_rect(struct pg_window *window, struct pg_rect bounds,
                    uint32_t color);
void pg_window_text(struct pg_window *window, uint32_t x, uint32_t y,
                    const char *text, uint32_t color);
bool pg_window_poll_event(struct pg_window *window, struct pg_event *event);
