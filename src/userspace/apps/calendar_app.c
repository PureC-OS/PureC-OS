#include "calendar_app.h"
#include "datetime_service.h"
#include "../display.h"

static const char *month_names[] = {
    "January",
    "February",
    "March",
    "April",
    "May",
    "June",
    "July",
    "August",
    "September",
    "October",
    "November",
    "December"
};

static bool editing;
static char input[11];
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

static uint8_t weekday(uint16_t year, uint8_t month, uint8_t day) {
    uint16_t year_in_century;
    uint16_t century;

    if (month < 3) {
        month += 12;
        year--;
    }

    year_in_century = year % 100;
    century = year / 100;
    return (uint8_t)(
        (day
            + (13 * (month + 1)) / 5
            + year_in_century
            + year_in_century / 4
            + century / 4
            + 5 * century
            + 6)
        % 7
    );
}

static void draw_grid(uint32_t window_x, uint32_t window_y) {
    const struct desktop_datetime *datetime = datetime_service_get();
    uint8_t first_weekday = weekday(datetime->year, datetime->month, 1);
    uint8_t first_column = first_weekday == 0 ? 6 : first_weekday - 1;
    uint8_t maximum_day = datetime_service_days_in_month(
        datetime->year,
        datetime->month
    );
    uint8_t day = 1;

    draw_text(
        window_x,
        window_y,
        42,
        108,
        "Mo Tu We Th Fr Sa Su",
        0x9399B2,
        8
    );

    for (uint8_t row = 0; row < 6 && day <= maximum_day; row++) {
        char line[24];
        uint8_t position = 0;

        for (uint8_t column = 0; column < 7; column++) {
            if ((row == 0 && column < first_column) || day > maximum_day) {
                line[position++] = ' ';
                line[position++] = ' ';
            } else {
                line[position++] = day >= 10
                    ? (char)('0' + day / 10)
                    : ' ';
                line[position++] = (char)('0' + day % 10);
                day++;
            }

            if (column < 6) {
                line[position++] = ' ';
            }
        }

        line[position] = '\0';
        draw_text(
            window_x,
            window_y,
            42,
            124 + row * 15,
            line,
            0xCDD6F4,
            8
        );
    }
}

void calendar_app_open(void) {
    editing = false;
    message = "";
    clear_input();
}

void calendar_app_draw(uint32_t window_x, uint32_t window_y) {
    const struct desktop_datetime *datetime = datetime_service_get();
    char text[20];

    datetime_service_format(text);
    text[10] = '\0';

    draw_text(window_x, window_y, 92, 42, text, 0xCDD6F4, 14);
    draw_text(
        window_x,
        window_y,
        120,
        76,
        month_names[datetime->month - 1],
        0xF9E2AF,
        12
    );
    draw_grid(window_x, window_y);
    draw_text(
        window_x,
        window_y,
        22,
        222,
        "Press E to set YYYY-MM-DD",
        0x9399B2,
        8
    );

    if (editing) {
        display_draw_rect(window_x + 15, window_y + 204, 330, 28, 0x313244);
        draw_text(window_x, window_y, 22, 213, input, 0xCDD6F4, 8);
    }
    draw_text(window_x, window_y, 18, 244, message, 0xA6E3A1, 8);
}

bool calendar_app_handle_key(char key) {
    if (!editing) {
        if (key != 'e' && key != 'E') {
            return false;
        }

        editing = true;
        message = "Enter date";
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
        const struct desktop_datetime *current = datetime_service_get();

        if (!datetime_service_parse(input, &parsed, true)) {
            message = "Invalid date";
            return true;
        }

        parsed.hour = current->hour;
        parsed.minute = current->minute;
        parsed.second = current->second;
        datetime_service_set(&parsed);
        message = "Saved to /purec/datetime.cfg";
        editing = false;
        return true;
    }
    if (key >= ' ' && key <= '~' && input_length + 1 < sizeof(input)) {
        input[input_length++] = key;
        input[input_length] = '\0';
    }

    return true;
}
