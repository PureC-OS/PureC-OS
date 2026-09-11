#include "desktop_apps.h"
#include "calculator_app.h"
#include "calendar_app.h"
#include "clock_app.h"
#include "datetime_service.h"
#include "../display.h"
#include "../../drivers/mouse/ps2_mouse.h"

#define APP_COUNT 3
#define WINDOW_WIDTH 360
#define WINDOW_HEIGHT 270
#define TITLE_BAR_HEIGHT 30

struct app_window {
    uint32_t x;
    uint32_t y;
    bool visible;
};

static struct app_window windows[APP_COUNT] = {
    {160, 70, false},
    {200, 100, false},
    {240, 130, false}
};
static uint8_t z_order[APP_COUNT] = {
    DESKTOP_APP_CLOCK,
    DESKTOP_APP_CALCULATOR,
    DESKTOP_APP_CALENDAR
};
static int8_t focused_app = -1;
static int8_t dragged_app = -1;
static int32_t drag_offset_x;
static int32_t drag_offset_y;
static uint16_t last_calendar_year;
static uint8_t last_calendar_month;
static uint8_t last_calendar_day;

static bool valid_app(int32_t app) {
    return app >= 0 && app < APP_COUNT;
}

static bool point_inside(
    int32_t point_x,
    int32_t point_y,
    uint32_t left,
    uint32_t top,
    uint32_t width,
    uint32_t height
) {
    return point_x >= (int32_t)left
        && point_y >= (int32_t)top
        && point_x < (int32_t)(left + width)
        && point_y < (int32_t)(top + height);
}

static const char *app_title(enum desktop_app app) {
    switch (app) {
        case DESKTOP_APP_CLOCK:
            return "Clock";
        case DESKTOP_APP_CALCULATOR:
            return "Calculator";
        case DESKTOP_APP_CALENDAR:
            return "Calendar";
    }

    return "Application";
}

static void draw_window_frame(enum desktop_app app) {
    const struct app_window *window = &windows[app];
    uint32_t title_color = focused_app == (int8_t)app
        ? 0x89B4FA
        : 0x585B70;

    display_draw_rect(
        window->x + 5,
        window->y + 5,
        WINDOW_WIDTH,
        WINDOW_HEIGHT,
        0x11111B
    );
    display_draw_rect(
        window->x,
        window->y,
        WINDOW_WIDTH,
        WINDOW_HEIGHT,
        0x45475A
    );
    display_draw_rect(
        window->x + 1,
        window->y + 1,
        WINDOW_WIDTH - 2,
        WINDOW_HEIGHT - 2,
        0x1E1E2E
    );
    display_draw_rect(
        window->x + 1,
        window->y + 1,
        WINDOW_WIDTH - 2,
        TITLE_BAR_HEIGHT,
        title_color
    );
    display_draw_text_at(
        window->x + 12,
        window->y + 10,
        app_title(app),
        0x1E1E2E,
        title_color
    );
    display_draw_rect(
        window->x + WINDOW_WIDTH - 27,
        window->y + 6,
        18,
        18,
        0xF38BA8
    );
    display_draw_text_at(
        window->x + WINDOW_WIDTH - 23,
        window->y + 10,
        "x",
        0x1E1E2E,
        0xF38BA8
    );
}

static void draw_app_content(enum desktop_app app) {
    const struct app_window *window = &windows[app];

    switch (app) {
        case DESKTOP_APP_CLOCK:
            clock_app_draw(window->x, window->y);
            break;
        case DESKTOP_APP_CALCULATOR:
            calculator_app_draw(window->x, window->y);
            break;
        case DESKTOP_APP_CALENDAR:
            calendar_app_draw(window->x, window->y);
            break;
    }
}

static void draw_window(enum desktop_app app) {
    draw_window_frame(app);
    draw_app_content(app);
}

static int8_t z_index_of(enum desktop_app app) {
    for (int8_t index = 0; index < APP_COUNT; index++) {
        if (z_order[index] == (uint8_t)app) {
            return index;
        }
    }
    return -1;
}

static bool bring_to_front(enum desktop_app app) {
    int8_t current_index = z_index_of(app);

    if (current_index < 0 || current_index == APP_COUNT - 1) {
        return false;
    }

    for (int8_t index = current_index; index < APP_COUNT - 1; index++) {
        z_order[index] = z_order[index + 1];
    }
    z_order[APP_COUNT - 1] = (uint8_t)app;
    return true;
}

static int8_t top_window_at(int32_t point_x, int32_t point_y) {
    for (int8_t index = APP_COUNT - 1; index >= 0; index--) {
        enum desktop_app app = (enum desktop_app)z_order[index];
        const struct app_window *window = &windows[app];

        if (window->visible && point_inside(
                point_x,
                point_y,
                window->x,
                window->y,
                WINDOW_WIDTH,
                WINDOW_HEIGHT)) {
            return (int8_t)app;
        }
    }

    return -1;
}

static int8_t top_visible_window(void) {
    for (int8_t index = APP_COUNT - 1; index >= 0; index--) {
        int8_t app = (int8_t)z_order[index];
        if (windows[app].visible) {
            return app;
        }
    }
    return -1;
}

static void redraw_from_z_index(int8_t first_index) {
    if (first_index < 0) {
        return;
    }

    mouse_begin_framebuffer_update();
    for (int8_t index = first_index; index < APP_COUNT; index++) {
        enum desktop_app app = (enum desktop_app)z_order[index];
        if (windows[app].visible) {
            draw_window(app);
        }
    }
    mouse_end_framebuffer_update();
}

static void open_app_state(enum desktop_app app) {
    switch (app) {
        case DESKTOP_APP_CLOCK:
            clock_app_open();
            break;
        case DESKTOP_APP_CALCULATOR:
            calculator_app_open();
            break;
        case DESKTOP_APP_CALENDAR:
            calendar_app_open();
            break;
    }
}

static bool handle_app_key(enum desktop_app app, char key) {
    switch (app) {
        case DESKTOP_APP_CLOCK:
            return clock_app_handle_key(key);
        case DESKTOP_APP_CALCULATOR:
            return calculator_app_handle_key(key);
        case DESKTOP_APP_CALENDAR:
            return calendar_app_handle_key(key);
    }

    return false;
}

static void remember_calendar_date(void) {
    const struct desktop_datetime *datetime = datetime_service_get();

    last_calendar_year = datetime->year;
    last_calendar_month = datetime->month;
    last_calendar_day = datetime->day;
}

void desktop_apps_init(void) {
    datetime_service_init();
    remember_calendar_date();
}

void desktop_apps_save_time(void) {
    datetime_service_save();
}

void desktop_apps_draw(void) {
    mouse_begin_framebuffer_update();
    for (int8_t index = 0; index < APP_COUNT; index++) {
        enum desktop_app app = (enum desktop_app)z_order[index];
        if (windows[app].visible) {
            draw_window(app);
        }
    }
    mouse_end_framebuffer_update();
}

void desktop_apps_open(
    enum desktop_app app,
    uint32_t screen_width,
    uint32_t screen_height
) {
    struct app_window *window;
    bool was_visible;

    if (!valid_app(app)) {
        return;
    }

    window = &windows[app];
    was_visible = window->visible;
    window->visible = true;

    if (window->x + WINDOW_WIDTH > screen_width) {
        window->x = 10 + (uint32_t)app * 24;
    }
    if (window->y + WINDOW_HEIGHT > screen_height) {
        window->y = 34 + (uint32_t)app * 24;
    }

    focused_app = (int8_t)app;
    bring_to_front(app);
    if (!was_visible) {
        open_app_state(app);
    }
    redraw_from_z_index(z_index_of(app));
}

bool desktop_apps_is_visible(void) {
    return top_visible_window() >= 0;
}

bool desktop_apps_contains_point(int32_t point_x, int32_t point_y) {
    return top_window_at(point_x, point_y) >= 0;
}

bool desktop_apps_handle_mouse(
    int32_t point_x,
    int32_t point_y,
    uint8_t buttons,
    bool pressed,
    bool released,
    uint32_t screen_width,
    uint32_t screen_height,
    bool *redraw_required
) {
    int8_t target_app = top_window_at(point_x, point_y);
    bool captured = dragged_app >= 0 || target_app >= 0;

    if (redraw_required != 0) {
        *redraw_required = false;
    }

    if (pressed && target_app >= 0) {
        enum desktop_app app = (enum desktop_app)target_app;
        struct app_window *window = &windows[app];

        focused_app = target_app;
        if (bring_to_front(app) && redraw_required != 0) {
            *redraw_required = true;
        }

        if (point_inside(
                point_x,
                point_y,
                window->x + WINDOW_WIDTH - 27,
                window->y + 6,
                18,
                18)) {
            window->visible = false;
            focused_app = top_visible_window();
            dragged_app = -1;
            if (redraw_required != 0) {
                *redraw_required = true;
            }
            return true;
        }

        if (point_inside(
                point_x,
                point_y,
                window->x,
                window->y,
                WINDOW_WIDTH,
                TITLE_BAR_HEIGHT)) {
            dragged_app = target_app;
            drag_offset_x = point_x - (int32_t)window->x;
            drag_offset_y = point_y - (int32_t)window->y;
        }
    }

    if (dragged_app >= 0 && (buttons & 1) != 0) {
        struct app_window *window = &windows[dragged_app];
        int32_t next_x = point_x - drag_offset_x;
        int32_t next_y = point_y - drag_offset_y;
        int32_t maximum_x = screen_width > WINDOW_WIDTH
            ? (int32_t)(screen_width - WINDOW_WIDTH)
            : 0;
        int32_t maximum_y = screen_height > WINDOW_HEIGHT
            ? (int32_t)(screen_height - WINDOW_HEIGHT)
            : 28;

        if (maximum_y < 28) {
            maximum_y = 28;
        }
        if (next_x < 0) {
            next_x = 0;
        }
        if (next_y < 28) {
            next_y = 28;
        }
        if (next_x > maximum_x) {
            next_x = maximum_x;
        }
        if (next_y > maximum_y) {
            next_y = maximum_y;
        }

        if ((uint32_t)next_x != window->x
            || (uint32_t)next_y != window->y) {
            window->x = (uint32_t)next_x;
            window->y = (uint32_t)next_y;
            if (redraw_required != 0) {
                *redraw_required = true;
            }
        }
        return true;
    }

    if (released && dragged_app >= 0) {
        dragged_app = -1;
        return true;
    }

    return captured;
}

bool desktop_apps_handle_key(char key) {
    const struct desktop_datetime *datetime;
    enum desktop_app app;
    int8_t first_redraw_index;
    uint16_t previous_year;
    uint8_t previous_month;
    uint8_t previous_day;
    bool date_changed;

    if (!valid_app(focused_app) || !windows[focused_app].visible) {
        focused_app = top_visible_window();
    }
    if (!valid_app(focused_app)) {
        return false;
    }

    app = (enum desktop_app)focused_app;
    datetime = datetime_service_get();
    previous_year = datetime->year;
    previous_month = datetime->month;
    previous_day = datetime->day;

    if (!handle_app_key(app, key)) {
        return false;
    }

    datetime = datetime_service_get();
    date_changed = datetime->year != previous_year
        || datetime->month != previous_month
        || datetime->day != previous_day;
    first_redraw_index = z_index_of(app);

    if (date_changed && windows[DESKTOP_APP_CALENDAR].visible) {
        int8_t calendar_index = z_index_of(DESKTOP_APP_CALENDAR);
        if (calendar_index < first_redraw_index) {
            first_redraw_index = calendar_index;
        }
    }

    remember_calendar_date();
    redraw_from_z_index(first_redraw_index);
    return true;
}

void desktop_apps_update(void) {
    const struct desktop_datetime *datetime;
    bool time_changed = datetime_service_update();
    bool date_changed;
    int8_t first_redraw_index = APP_COUNT;
    int8_t clock_index;
    int8_t calendar_index;

    if (!time_changed) {
        return;
    }

    datetime = datetime_service_get();
    date_changed = datetime->year != last_calendar_year
        || datetime->month != last_calendar_month
        || datetime->day != last_calendar_day;
    remember_calendar_date();

    clock_index = z_index_of(DESKTOP_APP_CLOCK);
    if (windows[DESKTOP_APP_CLOCK].visible
        && clock_index < first_redraw_index) {
        first_redraw_index = clock_index;
    }

    calendar_index = z_index_of(DESKTOP_APP_CALENDAR);
    if (date_changed
        && windows[DESKTOP_APP_CALENDAR].visible
        && calendar_index < first_redraw_index) {
        first_redraw_index = calendar_index;
    }

    if (first_redraw_index < APP_COUNT) {
        redraw_from_z_index(first_redraw_index);
    }
}
