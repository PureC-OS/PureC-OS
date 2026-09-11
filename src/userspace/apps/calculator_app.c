#include "calculator_app.h"
#include "../display.h"

static char input[40];
static uint32_t input_length;
static int64_t left_operand;
static char selected_operator;
static bool has_left_operand;
static const char *message = "";

static void clear_input(void) {
    input_length = 0;
    input[0] = '\0';
}

static bool append_input(char character) {
    if (input_length + 1 >= sizeof(input)) {
        return false;
    }

    input[input_length++] = character;
    input[input_length] = '\0';
    return true;
}

static int64_t parse_input(void) {
    int64_t result = 0;

    for (uint32_t index = 0; index < input_length; index++) {
        if (input[index] >= '0' && input[index] <= '9') {
            result = result * 10 + (input[index] - '0');
        }
    }

    return result;
}

static void format_result(int64_t result) {
    char reversed[24];
    uint32_t length = 0;
    bool negative = result < 0;
    uint64_t magnitude;

    if (negative) {
        magnitude = (uint64_t)(-(result + 1)) + 1;
    } else {
        magnitude = (uint64_t)result;
    }

    do {
        reversed[length++] = (char)('0' + magnitude % 10);
        magnitude /= 10;
    } while (magnitude != 0);

    clear_input();
    if (negative) {
        append_input('-');
    }
    while (length > 0) {
        append_input(reversed[--length]);
    }
}

static void select_operator(char operation) {
    left_operand = parse_input();
    selected_operator = operation;
    has_left_operand = true;
    clear_input();
}

static void evaluate(void) {
    int64_t right_operand = parse_input();
    int64_t result = 0;
    bool valid = true;

    switch (selected_operator) {
        case '+':
            result = left_operand + right_operand;
            break;
        case '-':
            result = left_operand - right_operand;
            break;
        case '*':
            result = left_operand * right_operand;
            break;
        case '/':
            if (right_operand == 0) {
                valid = false;
            } else {
                result = left_operand / right_operand;
            }
            break;
        default:
            valid = false;
            break;
    }

    format_result(result);
    message = valid ? "Result" : "Division by zero";
    has_left_operand = false;
}

void calculator_app_open(void) {
    clear_input();
    has_left_operand = false;
    message = "";
}

void calculator_app_draw(uint32_t window_x, uint32_t window_y) {
    const char *display = input[0] != '\0' ? input : "0";

    display_draw_text_sized_at(
        window_x + 20,
        window_y + 52,
        display,
        0xCDD6F4,
        0x1E1E2E,
        16
    );
    display_draw_text_sized_at(
        window_x + 20,
        window_y + 105,
        "Keyboard: 0-9  + - * /  Enter  C",
        0x9399B2,
        0x1E1E2E,
        8
    );
    display_draw_text_sized_at(
        window_x + 18,
        window_y + 244,
        message,
        0xA6E3A1,
        0x1E1E2E,
        8
    );
}

bool calculator_app_handle_key(char key) {
    bool is_operator = key == '+' || key == '-' || key == '*' || key == '/';

    if (key == 'c' || key == 'C') {
        calculator_app_open();
        return true;
    }
    if (key >= '0' && key <= '9') {
        append_input(key);
        return true;
    }
    if (is_operator && input_length > 0) {
        select_operator(key);
        return true;
    }
    if ((key == '\n' || key == '\r' || key == '=')
        && has_left_operand
        && input_length > 0) {
        evaluate();
        return true;
    }

    return true;
}
