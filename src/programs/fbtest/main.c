#include "../../libc/include/purec.h"
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define HID_ESCAPE 0x29
#define BOX 12

static struct fb_map_info fb;

static uint32_t background(uint32_t x, uint32_t y)
{
    uint32_t r = fb.width ? x * 255 / fb.width : 0;
    uint32_t g = fb.height ? y * 255 / fb.height : 0;
    uint32_t b = 160;
    return (r << 16) | (g << 8) | b;
}

static uint32_t *row(uint32_t y)
{
    return (uint32_t *)(uintptr_t)(fb.address + (uint64_t)y * fb.pitch_bytes);
}

static void fill_background(void)
{
    for (uint32_t y = 0; y < fb.height; y++) {
        uint32_t *line = row(y);
        for (uint32_t x = 0; x < fb.width; x++)
            line[x] = background(x, y);
    }
    pc_fb_damage(0, 0, fb.width, fb.height);
}

static void box(int32_t cx, int32_t cy, bool cursor, uint32_t color)
{
    int32_t x0 = cx < 0 ? 0 : cx, y0 = cy < 0 ? 0 : cy;
    int32_t x1 = cx + BOX, y1 = cy + BOX;
    if (x1 > (int32_t)fb.width)
        x1 = (int32_t)fb.width;
    if (y1 > (int32_t)fb.height)
        y1 = (int32_t)fb.height;
    if (x0 >= x1 || y0 >= y1)
        return;
    for (int32_t y = y0; y < y1; y++) {
        uint32_t *line = row((uint32_t)y);
        for (int32_t x = x0; x < x1; x++)
            line[x] = cursor ? color : background((uint32_t)x, (uint32_t)y);
    }
    pc_fb_damage((uint32_t)x0, (uint32_t)y0, (uint32_t)(x1 - x0), (uint32_t)(y1 - y0));
}

int main(void)
{
    int32_t status = pc_display_claim();
    if (status < 0) {
        fprintf(stderr, "fbtest: claim failed %d\n", status);
        return 1;
    }
    status = pc_fb_map(&fb);
    if (status < 0) {
        pc_display_release();
        fprintf(stderr, "fbtest: map failed %d\n", status);
        return 1;
    }
    int fd = pc_input_open();
    if (fd < 0) {
        pc_display_release();
        fprintf(stderr, "fbtest: input open failed %d\n", fd);
        return 1;
    }
    fill_background();
    int32_t mx = (int32_t)fb.width / 2, my = (int32_t)fb.height / 2;
    box(mx, my, true, 0xFFFFFF);
    unsigned keys = 0, moves = 0, clicks = 0;
    char last_keys[256] = "";
    time_t start = time(0);
    bool quit = false;
    while (!quit && time(0) - start < 30) {
        struct pollfd p = {.fd = fd, .events = POLLIN};
        if (poll(&p, 1, 100) <= 0)
            continue;
        struct input_record recs[16];
        ssize_t got = read(fd, recs, sizeof(recs));
        if (got <= 0)
            continue;
        for (int i = 0; i < (int)(got / (ssize_t)sizeof(recs[0])); i++) {
            struct input_record *r = &recs[i];
            if (r->type == INPUT_RECORD_KEY) {
                keys++;
                if (r->pressed && strlen(last_keys) + 6 < sizeof(last_keys)) {
                    char t[8];
                    snprintf(t, sizeof(t), "%02x ", r->scancode);
                    strcat(last_keys, t);
                }
                if (r->pressed && r->scancode == HID_ESCAPE)
                    quit = true;
            } else if (r->type == INPUT_RECORD_MOUSE) {
                moves++;
                if (r->buttons)
                    clicks++;
                box(mx, my, false, 0);
                mx = r->x;
                my = r->y;
                box(mx, my, true, r->buttons ? 0xFF4040 : 0xFFFFFF);
            }
        }
    }
    close(fd);
    pc_display_release();
    printf("fbtest: %ux%u pitch=%u bpp=%u map=%p gen=%u\n", fb.width, fb.height,
           fb.pitch_bytes, fb.bpp, (void *)(uintptr_t)fb.address, fb.generation);
    printf("fbtest: key events=%u mouse events=%u with buttons=%u\n", keys, moves, clicks);
    printf("fbtest: keys pressed: %s\n", last_keys);
    printf("fbtest: %s\n", quit ? "quit by Escape" : "timeout 30 s");
    return 0;
}
