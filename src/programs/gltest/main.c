#include "../../libc/include/purec.h"
#include "../../libgui/include/puregui.h"

static float gl_sin(float x) {
    const float pi = 3.14159265f;
    const float tau = 6.28318530f;
    while (x > pi) x -= tau;
    while (x < -pi) x += tau;
    float x2 = x * x;
    float x3 = x2 * x;
    float x5 = x3 * x2;
    float x7 = x5 * x2;
    return x - x3 * 0.16666666f + x5 * 0.00833333f - x7 * 0.00019841f;
}

static float gl_cos(float x) {
    const float half_pi = 1.57079632f;
    return gl_sin(x + half_pi);
}

static void draw_pixel(struct pg_window *win, int x, int y, uint32_t color) {
    if (x < 0 || y < 0) return;
    struct pg_rect c = pg_window_client(win);
    if ((uint32_t)x >= c.width || (uint32_t)y >= c.height) return;
    struct pg_rect r;
    r.x = (uint32_t)x;
    r.y = (uint32_t)y;
    r.width = 2;
    r.height = 2;
    pg_window_rect(win, r, color);
}

static void draw_line(struct pg_window *win, int x0, int y0, int x1, int y1, uint32_t color) {
    int dx = x1 - x0;
    if (dx < 0) dx = -dx;
    int dy = y1 - y0;
    if (dy < 0) dy = -dy;
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    int x = x0;
    int y = y0;
    for (;;) {
        draw_pixel(win, x, y, color);
        if (x == x1 && y == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x += sx; }
        if (e2 < dx) { err += dx; y += sy; }
    }
}

void _start(void) {
    struct pg_window window;
    if (!pg_window_center(&window, "GL Test - wire cube", 480, 360)) pc_exit(1);

    static const float vx[8] = {-1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f};
    static const float vy[8] = {-1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f};
    static const float vz[8] = {-1.0f, -1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    static const uint8_t edges[12][2] = {
        {0, 1}, {1, 2}, {2, 3}, {3, 0},
        {4, 5}, {5, 6}, {6, 7}, {7, 4},
        {0, 4}, {1, 5}, {2, 6}, {3, 7}
    };

    float angle = 0.0f;
    struct pg_event event;
    event.type = PG_EVENT_NONE;

    while (pg_window_is_open(&window)) {
        while (pg_window_poll_event(&window, &event)) {
            if (event.type == PG_EVENT_CLOSE) {
                pg_window_close(&window);
                pc_exit(0);
            }
            if (event.type == PG_EVENT_KEY && (event.key == 27 || event.key == 'q' || event.key == 'Q')) {
                pg_window_close(&window);
                pc_exit(0);
            }
        }

        float ca = gl_cos(angle);
        float sa = gl_sin(angle);
        float cb = gl_cos(angle * 0.7f);
        float sb = gl_sin(angle * 0.7f);

        int px[8];
        int py[8];
        for (int i = 0; i < 8; i++) {
            float x1 = vx[i] * ca + vz[i] * sa;
            float z1 = vz[i] * ca - vx[i] * sa;
            float y1 = vy[i] * cb - z1 * sb;
            float z2 = z1 * cb + vy[i] * sb;
            float dist = 4.0f + z2;
            if (dist < 0.5f) dist = 0.5f;
            float sx = x1 / dist;
            float sy = y1 / dist;
            px[i] = (int)(240.0f + sx * 180.0f);
            py[i] = (int)(180.0f - sy * 180.0f);
        }

        pg_window_begin(&window);
        pg_window_clear(&window, 0x001E1E2E);
        pg_window_text(&window, 10, 8, "soft3d: no kernel changes, q to quit", 0x00CDD6F4);
        for (int e = 0; e < 12; e++) {
            int a = edges[e][0];
            int b = edges[e][1];
            draw_line(&window, px[a], py[a], px[b], py[b], 0x0089B4FA);
        }
        for (int i = 0; i < 8; i++) draw_pixel(&window, px[i], py[i], 0x00F38BA8);
        pg_window_end(&window);

        angle += 0.06f;
        if (angle > 6.28318530f) angle -= 6.28318530f;
        pc_sleep(32);
    }

    pc_exit(0);
}
