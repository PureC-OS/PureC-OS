#include "../../libc/include/purec.h"
#include "../../libc/include/hosted/math.h"
#include "../../libgui/include/puregui.h"

#define FRAME_W 480u
#define FRAME_H 360u
#define COLOR_BACKGROUND 0x001E1E2Eu
#define COLOR_EDGE 0x0089B4FAu
#define COLOR_VERTEX 0x00F38BA8u
#define COLOR_TEXT 0x00CDD6F4u
#define FRAME_DELAY_MS 16u

static uint32_t frame[FRAME_W * FRAME_H];

static void put_pixel(int x, int y, uint32_t color) {
    if (x < 0 || y < 0 || x >= (int)FRAME_W || y >= (int)FRAME_H) return;
    frame[(uint32_t)y * FRAME_W + (uint32_t)x] = color;
}

static void fill_frame(uint32_t color) {
    for (uint32_t i = 0; i < FRAME_W * FRAME_H; i++) frame[i] = color;
}

static void draw_line(int x0, int y0, int x1, int y1, uint32_t color) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y1 - y0 : y0 - y1;
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    for (;;) {
        put_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx) { err += dx; y0 += sy; }
    }
}

static void draw_dot(int x, int y, uint32_t color) {
    for (int oy = -1; oy <= 1; oy++)
        for (int ox = -1; ox <= 1; ox++) put_pixel(x + ox, y + oy, color);
}

static uint64_t uptime_ms(void) {
    struct time_info info;
    if (pc_time_get(&info) != 0) return 0;
    return info.uptime_ms;
}

static void format_number(char *out, uint32_t value) {
    char digits[12];
    uint32_t count = 0;
    do {
        digits[count++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value && count < sizeof(digits));
    for (uint32_t i = 0; i < count; i++) out[i] = digits[count - 1 - i];
    out[count] = '\0';
}

static void build_status(char *out, uint32_t fps, uint32_t frame_ms) {
    char number[12];
    uint32_t length = 0;
    const char *prefix = "soft3d via pg_window_blit  fps ";
    for (; prefix[length]; length++) out[length] = prefix[length];
    format_number(number, fps);
    for (uint32_t i = 0; number[i]; i++) out[length++] = number[i];
    const char *middle = "  frame ms ";
    for (uint32_t i = 0; middle[i]; i++) out[length++] = middle[i];
    format_number(number, frame_ms);
    for (uint32_t i = 0; number[i]; i++) out[length++] = number[i];
    out[length] = '\0';
}

void _start(void) {
    struct pg_window window;
    if (!pg_window_center(&window, "GL Test - wire cube", FRAME_W, FRAME_H)) pc_exit(1);

    static const float vx[8] = {-1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f};
    static const float vy[8] = {-1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f};
    static const float vz[8] = {-1.0f, -1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    static const uint8_t edges[12][2] = {
        {0, 1}, {1, 2}, {2, 3}, {3, 0},
        {4, 5}, {5, 6}, {6, 7}, {7, 4},
        {0, 4}, {1, 5}, {2, 6}, {3, 7}
    };

    float angle = 0.0f;
    uint64_t second_start = uptime_ms();
    uint32_t frames_this_second = 0;
    uint32_t fps = 0;
    uint32_t frame_ms = 0;
    char status[64];
    build_status(status, 0, 0);
    struct pg_event event;
    event.type = PG_EVENT_NONE;

    while (pg_window_is_open(&window)) {
        while (pg_window_poll_event(&window, &event)) {
            if (event.type == PG_EVENT_CLOSE ||
                (event.type == PG_EVENT_KEY &&
                 (event.key == 27 || event.key == 'q' || event.key == 'Q'))) {
                pg_window_close(&window);
                pc_exit(0);
            }
        }

        uint64_t frame_start = uptime_ms();
        float ca = cosf(angle);
        float sa = sinf(angle);
        float cb = cosf(angle * 0.7f);
        float sb = sinf(angle * 0.7f);

        int px[8];
        int py[8];
        for (int i = 0; i < 8; i++) {
            float x1 = vx[i] * ca + vz[i] * sa;
            float z1 = vz[i] * ca - vx[i] * sa;
            float y1 = vy[i] * cb - z1 * sb;
            float z2 = z1 * cb + vy[i] * sb;
            float dist = 4.0f + z2;
            if (dist < 0.5f) dist = 0.5f;
            px[i] = (int)((float)FRAME_W / 2.0f + x1 / dist * 180.0f);
            py[i] = (int)((float)FRAME_H / 2.0f - y1 / dist * 180.0f);
        }

        fill_frame(COLOR_BACKGROUND);
        for (int e = 0; e < 12; e++) {
            int a = edges[e][0];
            int b = edges[e][1];
            draw_line(px[a], py[a], px[b], py[b], COLOR_EDGE);
        }
        for (int i = 0; i < 8; i++) draw_dot(px[i], py[i], COLOR_VERTEX);

        pg_window_begin(&window);
        pg_window_blit(&window, 0, 0, FRAME_W, FRAME_H, frame, FRAME_W);
        pg_window_text(&window, 10, 8, status, COLOR_TEXT);
        pg_window_end(&window);

        uint64_t now = uptime_ms();
        frame_ms = (uint32_t)(now - frame_start);
        frames_this_second++;
        if (now - second_start >= 1000) {
            fps = frames_this_second;
            frames_this_second = 0;
            second_start = now;
            build_status(status, fps, frame_ms);
        }

        angle += 0.04f;
        if (angle > 6.28318530f) angle -= 6.28318530f;
        pc_sleep(FRAME_DELAY_MS);
    }

    pc_exit(0);
}
