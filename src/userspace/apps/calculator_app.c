#include "calculator_app.h"
#include "../display.h"

#define CALC_INPUT_CAP 40
#define CALC_EXPR_CAP 64
#define CALC_MSG_CAP 32
#define CALC_COLS 4
#define CALC_ROWS 5
#define CALC_BTN_COUNT 20
#define CALC_DISP_X 12
#define CALC_DISP_Y 40
#define CALC_DISP_W 336
#define CALC_DISP_H 56
#define CALC_GRID_X 12
#define CALC_GRID_Y 110
#define CALC_BTN_W 79
#define CALC_BTN_H 26
#define CALC_GAP_X 6
#define CALC_GAP_Y 6

#define CALC_BG 0x1E1E2E
#define CALC_DISP_BG 0x11111B
#define CALC_TEXT 0xCDD6F4
#define CALC_DIM 0x9399B2
#define CALC_OK 0xA6E3A1
#define CALC_ERR 0xF38BA8
#define CALC_DIGIT_BG 0x45475A
#define CALC_UTIL_BG 0x585B70
#define CALC_OP_BG 0x89B4FA
#define CALC_EQ_BG 0xA6E3A1
#define CALC_DARK 0x1E1E2E

static const char *calc_labels[CALC_BTN_COUNT] = {
    "C", "CE", "<-", "/",
    "7", "8", "9", "*",
    "4", "5", "6", "-",
    "1", "2", "3", "+",
    "+-", "0", "%", "="
};

static char calc_input[CALC_INPUT_CAP];
static uint32_t calc_len;
static int64_t calc_left;
static char calc_op;
static bool calc_has_left;
static bool calc_fresh;
static bool calc_error;
static char calc_expr[CALC_EXPR_CAP];
static char calc_msg[CALC_MSG_CAP];
static char calc_last_op;
static int64_t calc_last_right;

static uint32_t calc_strlen(const char *s) {
    uint32_t n = 0;
    while (s[n]) {
        n++;
    }
    return n;
}

static void calc_msg_set(const char *s) {
    uint32_t i = 0;
    while (s[i] && i + 1 < CALC_MSG_CAP) {
        calc_msg[i] = s[i];
        i++;
    }
    calc_msg[i] = '\0';
}

static void calc_clear_input(void) {
    calc_len = 0;
    calc_input[0] = '\0';
}

static void calc_format_int64(int64_t value, char *dst, uint32_t cap) {
    char rev[24];
    uint32_t n = 0;
    bool neg = value < 0;
    uint64_t mag;
    uint32_t pos = 0;
    uint32_t i;
    if (cap == 0) {
        return;
    }
    if (neg) {
        mag = (uint64_t)(-(value + 1)) + 1;
    } else {
        mag = (uint64_t)value;
    }
    do {
        rev[n++] = (char)('0' + mag % 10);
        mag /= 10;
    } while (mag != 0);
    if (neg && pos + 1 < cap) {
        dst[pos++] = '-';
    }
    i = n;
    while (i > 0 && pos + 1 < cap) {
        i--;
        dst[pos++] = rev[i];
    }
    dst[pos] = '\0';
}

static bool calc_parse_input(int64_t *out) {
    uint32_t i = 0;
    bool neg = false;
    __int128 acc = 0;
    if (calc_len == 0) {
        return false;
    }
    if (calc_input[0] == '-') {
        if (calc_len == 1) {
            return false;
        }
        neg = true;
        i = 1;
    }
    for (; i < calc_len; i++) {
        char c = calc_input[i];
        if (c < '0' || c > '9') {
            return false;
        }
        acc = acc * 10 + (c - '0');
        if (!neg && acc > (__int128)9223372036854775807) {
            return false;
        }
        if (neg && acc > (__int128)9223372036854775807 + 1) {
            return false;
        }
    }
    if (neg && acc == (__int128)9223372036854775807 + 1) {
        *out = (int64_t)(-9223372036854775807LL - 1);
        return true;
    }
    if (neg) {
        *out = -(int64_t)acc;
    } else {
        *out = (int64_t)acc;
    }
    return true;
}

static int calc_apply(int64_t a, char op, int64_t b, int64_t *out) {
    __int128 t;
    if (op == '+') {
        t = (__int128)a + (__int128)b;
        if (t > (__int128)9223372036854775807 || t < (__int128)(-9223372036854775807LL - 1)) {
            return 2;
        }
        *out = (int64_t)t;
        return 0;
    }
    if (op == '-') {
        t = (__int128)a - (__int128)b;
        if (t > (__int128)9223372036854775807 || t < (__int128)(-9223372036854775807LL - 1)) {
            return 2;
        }
        *out = (int64_t)t;
        return 0;
    }
    if (op == '*') {
        t = (__int128)a * (__int128)b;
        if (t > (__int128)9223372036854775807 || t < (__int128)(-9223372036854775807LL - 1)) {
            return 2;
        }
        *out = (int64_t)t;
        return 0;
    }
    if (op == '/') {
        if (b == 0) {
            return 1;
        }
        if (a == (-9223372036854775807LL - 1) && b == -1) {
            return 2;
        }
        *out = a / b;
        return 0;
    }
    if (op == '%') {
        if (b == 0) {
            return 1;
        }
        if (a == (-9223372036854775807LL - 1) && b == -1) {
            return 2;
        }
        *out = a % b;
        return 0;
    }
    return 1;
}

static void calc_set_error(const char *msg) {
    calc_error = true;
    calc_msg_set(msg);
    calc_clear_input();
    calc_has_left = false;
    calc_op = 0;
    calc_fresh = false;
    calc_last_op = 0;
}

static void calc_build_pending(void) {
    char num[24];
    uint32_t i = 0;
    uint32_t j = 0;
    calc_format_int64(calc_left, num, sizeof(num));
    while (num[j] && i + 1 < CALC_EXPR_CAP) {
        calc_expr[i++] = num[j++];
    }
    if (i + 2 < CALC_EXPR_CAP) {
        calc_expr[i++] = ' ';
        calc_expr[i++] = calc_op;
    }
    calc_expr[i] = '\0';
}

static void calc_build_result(int64_t right) {
    char a[24];
    char b[24];
    uint32_t i = 0;
    uint32_t j = 0;
    calc_format_int64(calc_left, a, sizeof(a));
    calc_format_int64(right, b, sizeof(b));
    j = 0;
    while (a[j] && i + 1 < CALC_EXPR_CAP) {
        calc_expr[i++] = a[j++];
    }
    if (i + 1 < CALC_EXPR_CAP) {
        calc_expr[i++] = ' ';
    }
    if (i + 1 < CALC_EXPR_CAP) {
        calc_expr[i++] = calc_last_op ? calc_last_op : calc_op;
    }
    if (i + 1 < CALC_EXPR_CAP) {
        calc_expr[i++] = ' ';
    }
    j = 0;
    while (b[j] && i + 1 < CALC_EXPR_CAP) {
        calc_expr[i++] = b[j++];
    }
    if (i + 2 < CALC_EXPR_CAP) {
        calc_expr[i++] = ' ';
        calc_expr[i++] = '=';
    }
    calc_expr[i] = '\0';
}

static void calc_clear_all(void) {
    calc_clear_input();
    calc_has_left = false;
    calc_op = 0;
    calc_fresh = false;
    calc_error = false;
    calc_expr[0] = '\0';
    calc_msg[0] = '\0';
    calc_last_op = 0;
    calc_last_right = 0;
}

static void calc_clear_entry(void) {
    if (calc_error) {
        calc_error = false;
    }
    calc_clear_input();
    calc_fresh = false;
    calc_msg[0] = '\0';
}

static void calc_backspace(void) {
    if (calc_error) {
        calc_clear_all();
        return;
    }
    if (calc_len > 0) {
        calc_len--;
        calc_input[calc_len] = '\0';
    }
    calc_fresh = false;
}

static void calc_toggle_sign(void) {
    uint32_t i;
    if (calc_error || calc_len == 0) {
        return;
    }
    if (calc_len == 1 && calc_input[0] == '0') {
        return;
    }
    if (calc_input[0] == '-') {
        for (i = 0; i < calc_len - 1; i++) {
            calc_input[i] = calc_input[i + 1];
        }
        calc_len--;
        calc_input[calc_len] = '\0';
        return;
    }
    if (calc_len + 1 >= CALC_INPUT_CAP) {
        return;
    }
    for (i = calc_len; i > 0; i--) {
        calc_input[i] = calc_input[i - 1];
    }
    calc_input[0] = '-';
    calc_len++;
    calc_input[calc_len] = '\0';
}

static bool calc_press_digit(char d) {
    int64_t probe;
    if (calc_error) {
        calc_clear_all();
    }
    if (calc_fresh) {
        calc_clear_input();
        calc_fresh = false;
        calc_expr[0] = '\0';
        calc_last_op = 0;
        calc_msg[0] = '\0';
    }
    if (calc_len == 1 && calc_input[0] == '0') {
        if (d == '0') {
            return true;
        }
        calc_input[0] = d;
        calc_msg[0] = '\0';
        return true;
    }
    if (calc_len == 2 && calc_input[0] == '-' && calc_input[1] == '0') {
        if (d == '0') {
            return true;
        }
        calc_input[1] = d;
        calc_msg[0] = '\0';
        return true;
    }
    if (calc_len + 1 >= CALC_INPUT_CAP) {
        calc_msg_set("Too long");
        return true;
    }
    calc_input[calc_len++] = d;
    calc_input[calc_len] = '\0';
    if (!calc_parse_input(&probe)) {
        (void)probe;
        calc_len--;
        calc_input[calc_len] = '\0';
        calc_msg_set("Overflow");
        return true;
    }
    if (calc_msg[0] != '\0') {
        calc_msg[0] = '\0';
    }
    return true;
}

static bool calc_select_operator(char next) {
    int64_t cur = 0;
    if (calc_error) {
        return true;
    }
    if (calc_len == 0) {
        if (calc_has_left) {
            calc_op = next;
            calc_build_pending();
            calc_msg[0] = '\0';
            return true;
        }
        if (next == '-' && !calc_has_left) {
            calc_input[0] = '-';
            calc_input[1] = '\0';
            calc_len = 1;
            return true;
        }
        return false;
    }
    if (!calc_parse_input(&cur)) {
        calc_set_error("Overflow");
        return true;
    }
    if (calc_has_left && !calc_fresh) {
        int64_t res = 0;
        int rc = calc_apply(calc_left, calc_op, cur, &res);
        if (rc == 1) {
            calc_left = calc_left;
            calc_last_op = calc_op;
            calc_build_result(cur);
            calc_set_error("Div by zero");
            return true;
        }
        if (rc == 2) {
            calc_build_result(cur);
            calc_set_error("Overflow");
            return true;
        }
        calc_left = res;
    } else {
        calc_left = cur;
    }
    calc_has_left = true;
    calc_op = next;
    calc_clear_input();
    calc_fresh = false;
    calc_last_op = 0;
    calc_build_pending();
    calc_msg[0] = '\0';
    return true;
}

static bool calc_evaluate(void) {
    int64_t right = 0;
    int64_t res = 0;
    int rc;
    char saved;
    if (calc_error) {
        return true;
    }
    if (calc_has_left && calc_len > 0) {
        if (!calc_parse_input(&right)) {
            calc_build_result(0);
            calc_set_error("Overflow");
            return true;
        }
        saved = calc_op;
        rc = calc_apply(calc_left, calc_op, right, &res);
        calc_last_op = saved;
        calc_last_right = right;
        if (rc == 1) {
            calc_build_result(right);
            calc_set_error("Div by zero");
            return true;
        }
        if (rc == 2) {
            calc_build_result(right);
            calc_set_error("Overflow");
            return true;
        }
        calc_build_result(right);
        calc_format_int64(res, calc_input, CALC_INPUT_CAP);
        calc_len = calc_strlen(calc_input);
        calc_has_left = false;
        calc_op = 0;
        calc_fresh = true;
        calc_msg_set("Result");
        return true;
    }
    if (!calc_has_left && calc_fresh && calc_last_op && calc_len > 0) {
        int64_t base = 0;
        if (!calc_parse_input(&base)) {
            calc_set_error("Overflow");
            return true;
        }
        calc_left = base;
        rc = calc_apply(base, calc_last_op, calc_last_right, &res);
        if (rc == 1) {
            calc_build_result(calc_last_right);
            calc_set_error("Div by zero");
            return true;
        }
        if (rc == 2) {
            calc_build_result(calc_last_right);
            calc_set_error("Overflow");
            return true;
        }
        calc_build_result(calc_last_right);
        calc_format_int64(res, calc_input, CALC_INPUT_CAP);
        calc_len = calc_strlen(calc_input);
        calc_fresh = true;
        calc_msg_set("Result");
        return true;
    }
    return false;
}

static int calc_button_at(uint32_t window_x, uint32_t window_y, int32_t px, int32_t py) {
    int32_t gx = (int32_t)(window_x + CALC_GRID_X);
    int32_t gy = (int32_t)(window_y + CALC_GRID_Y);
    int32_t dx;
    int32_t dy;
    int32_t col;
    int32_t row;
    int32_t stride_x = (int32_t)(CALC_BTN_W + CALC_GAP_X);
    int32_t stride_y = (int32_t)(CALC_BTN_H + CALC_GAP_Y);
    if (px < gx || py < gy) {
        return -1;
    }
    dx = px - gx;
    dy = py - gy;
    col = dx / stride_x;
    row = dy / stride_y;
    if (col < 0 || col >= CALC_COLS || row < 0 || row >= CALC_ROWS) {
        return -1;
    }
    if (dx - col * stride_x >= (int32_t)CALC_BTN_W) {
        return -1;
    }
    if (dy - row * stride_y >= (int32_t)CALC_BTN_H) {
        return -1;
    }
    return row * CALC_COLS + col;
}

static bool calc_press_button(int id) {
    const char *label;
    if (id < 0 || id >= CALC_BTN_COUNT) {
        return false;
    }
    label = calc_labels[id];
    if (label[1] == '\0' && label[0] >= '0' && label[0] <= '9') {
        return calc_press_digit(label[0]);
    }
    if (label[0] == '+' && label[1] == '\0') {
        return calc_select_operator('+');
    }
    if (label[0] == '-' && label[1] == '\0') {
        return calc_select_operator('-');
    }
    if (label[0] == '*' && label[1] == '\0') {
        return calc_select_operator('*');
    }
    if (label[0] == '/' && label[1] == '\0') {
        return calc_select_operator('/');
    }
    if (label[0] == '%' && label[1] == '\0') {
        return calc_select_operator('%');
    }
    if (label[0] == '=' && label[1] == '\0') {
        return calc_evaluate();
    }
    if (label[0] == 'C' && label[1] == '\0') {
        calc_clear_all();
        return true;
    }
    if (label[0] == 'C' && label[1] == 'E') {
        calc_clear_entry();
        return true;
    }
    if (label[0] == '<') {
        calc_backspace();
        return true;
    }
    if (label[0] == '+' && label[1] == '-') {
        calc_toggle_sign();
        return true;
    }
    return false;
}

static void calc_button_colors(int id, uint32_t *bg, uint32_t *fg) {
    const char *label = calc_labels[id];
    *bg = CALC_DIGIT_BG;
    *fg = CALC_TEXT;
    if (label[0] == 'C' || label[0] == '<') {
        *bg = CALC_ERR;
        *fg = CALC_DARK;
        return;
    }
    if (label[0] == 'C' && label[1] == 'E') {
        *bg = CALC_ERR;
        *fg = CALC_DARK;
        return;
    }
    if (label[0] == '+' || label[0] == '-' || label[0] == '*' || label[0] == '/') {
        if (label[1] == '\0') {
            *bg = CALC_OP_BG;
            *fg = CALC_DARK;
            return;
        }
    }
    if (label[0] == '%' && label[1] == '\0') {
        *bg = CALC_OP_BG;
        *fg = CALC_DARK;
        return;
    }
    if (label[0] == '=' && label[1] == '\0') {
        *bg = CALC_EQ_BG;
        *fg = CALC_DARK;
        return;
    }
    if (label[0] == '+' && label[1] == '-') {
        *bg = CALC_UTIL_BG;
        *fg = CALC_TEXT;
        return;
    }
}

static void calc_draw_right(uint32_t y, uint32_t right_x, uint32_t left_x, const char *text, uint32_t fg, uint32_t bg, uint32_t size) {
    uint32_t n = calc_strlen(text);
    uint32_t max_chars;
    uint32_t shown;
    uint32_t start = 0;
    uint32_t x;
    if (n == 0 || size == 0) {
        return;
    }
    if (right_x <= left_x) {
        return;
    }
    max_chars = (right_x - left_x) / size;
    if (max_chars == 0) {
        return;
    }
    shown = n;
    if (shown > max_chars) {
        shown = max_chars;
        start = n - shown;
    }
    x = right_x - shown * size;
    display_draw_text_sized_at(x, y, text + start, fg, bg, size);
}

void calculator_app_open(void) {
    calc_clear_all();
}

void calculator_app_draw(uint32_t window_x, uint32_t window_y) {
    uint32_t dx = window_x + CALC_DISP_X;
    uint32_t dy = window_y + CALC_DISP_Y;
    uint32_t right = dx + CALC_DISP_W - 8;
    uint32_t left = dx + 8;
    const char *main_text = calc_input[0] ? calc_input : "0";
    uint32_t main_fg = CALC_TEXT;
    uint32_t msg_color = CALC_OK;
    int i;
    if (calc_error) {
        main_text = "Error";
        main_fg = CALC_ERR;
        msg_color = CALC_ERR;
    }
    display_draw_rect(dx, dy, CALC_DISP_W, CALC_DISP_H, CALC_DISP_BG);
    if (calc_expr[0]) {
        calc_draw_right(dy + 6, right, left, calc_expr, CALC_DIM, CALC_DISP_BG, 8);
    }
    calc_draw_right(dy + 20, right, left, main_text, main_fg, CALC_DISP_BG, 16);
    if (calc_msg[0]) {
        display_draw_text_sized_at(window_x + CALC_DISP_X + 8, dy + CALC_DISP_H - 12, calc_msg, msg_color, CALC_DISP_BG, 8);
    }
    for (i = 0; i < CALC_BTN_COUNT; i++) {
        uint32_t bx = window_x + CALC_GRID_X + (uint32_t)(i % CALC_COLS) * (CALC_BTN_W + CALC_GAP_X);
        uint32_t by = window_y + CALC_GRID_Y + (uint32_t)(i / CALC_COLS) * (CALC_BTN_H + CALC_GAP_Y);
        uint32_t bg;
        uint32_t fg;
        uint32_t lw = calc_strlen(calc_labels[i]) * 8;
        uint32_t tx = bx + (CALC_BTN_W > lw ? (CALC_BTN_W - lw) / 2 : 2);
        uint32_t ty = by + (CALC_BTN_H - 8) / 2;
        calc_button_colors(i, &bg, &fg);
        display_draw_rect(bx, by, CALC_BTN_W, CALC_BTN_H, bg);
        display_draw_text_at(tx, ty, calc_labels[i], fg, bg);
    }
    (void)CALC_BG;
}

bool calculator_app_handle_key(char key) {
    if (key >= '0' && key <= '9') {
        return calc_press_digit(key);
    }
    if (key == '+' || key == '-') {
        return calc_select_operator(key);
    }
    if (key == '*' || key == 'x' || key == 'X') {
        return calc_select_operator('*');
    }
    if (key == '/') {
        return calc_select_operator('/');
    }
    if (key == '%') {
        return calc_select_operator('%');
    }
    if (key == '=' || key == '\n' || key == '\r') {
        return calc_evaluate();
    }
    if (key == 'c' || key == 'C' || key == 27) {
        calc_clear_all();
        return true;
    }
    if (key == 'e' || key == 'E') {
        calc_clear_entry();
        return true;
    }
    if (key == '\b' || key == 127) {
        calc_backspace();
        return true;
    }
    if (key == 'n' || key == 'N' || key == '_') {
        calc_toggle_sign();
        return true;
    }
    return false;
}

bool calculator_app_handle_click(uint32_t window_x, uint32_t window_y, int32_t point_x, int32_t point_y) {
    int id = calc_button_at(window_x, window_y, point_x, point_y);
    if (id < 0) {
        return false;
    }
    return calc_press_button(id);
}
