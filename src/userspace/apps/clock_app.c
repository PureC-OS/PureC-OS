#include "clock_app.h"
#include "datetime_service.h"
#include "../display.h"

static bool editing;
static char input[20];
static uint32_t input_length;
static const char *message = "";

static void clear_input(void) {
    input_length = 0;
    input[0] = '\0';
}

static void draw_text(
    uint32_t window_x,
    uint32_t window_y,
    uint32_t offset_x,
    uint32_t offset_y,
    const char *text,
    uint32_t color,
    uint32_t size
) {
    display_draw_text_sized_at(
        window_x + offset_x,
        window_y + offset_y,
        text,
        color,
        0x1E1E2E,
        size
    );
}

void clock_app_open(void) {
    editing = false;
    message = "";
    clear_input();
}

void clock_app_draw(uint32_t window_x, uint32_t window_y) {
    char text[20];

    datetime_service_format(text);
    draw_text(window_x, window_y, 28, 70, text, 0xCDD6F4, 18);
    draw_text(
        window_x,
        window_y,
        22,
        130,
        "Press E to set YYYY-MM-DD HH:MM:SS",
        0x9399B2,
        8
    );

    if (editing) {
        display_draw_rect(window_x + 15, window_y + 204, 330, 28, 0x313244);
        draw_text(window_x, window_y, 22, 213, input, 0xCDD6F4, 8);
    }
    draw_text(window_x, window_y, 18, 244, message, 0xA6E3A1, 8);
}

bool clock_app_handle_key(char key) {
    if (!editing) {
        if (key != 'e' && key != 'E') {
            return false;
        }

        editing = true;
        message = "Enter full date and time";
        clear_input();
        return true;
    }

    if (key == 27) {
        editing = false;
        return true;
    }
    if ((key == '\b' || key == 127) && input_length > 0) {
        input[--input_length] = '\0';
        return true;
    }
    if (key == '\n' || key == '\r') {
        struct desktop_datetime parsed;

        if (datetime_service_parse(input, &parsed, false)) {
            datetime_service_set(&parsed);
            message = "Saved to /purec/datetime.cfg";
            editing = false;
        } else {
            message = "Invalid date/time";
        }
        return true;
    }
    if (key >= ' ' && key <= '~' && input_length + 1 < sizeof(input)) {
        input[input_length++] = key;
        input[input_length] = '\0';
    }

    return true;
}
