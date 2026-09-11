#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define WIDTH 320
#define HEIGHT 180

static void write_u16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

static void write_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

int main(int argc, char **argv) {
    const char *out_path = "src/demo/screenshot.bmp";
    if (argc > 1) out_path = argv[1];

    uint32_t row_bytes = (WIDTH * 3 + 3) & ~3U;
    uint32_t image_size = row_bytes * HEIGHT;
    uint32_t file_size = 54 + image_size;

    uint8_t *buf = (uint8_t *)calloc(1, file_size);
    if (!buf) return 1;

    // BMP Header
    buf[0] = 'B'; buf[1] = 'M';
    write_u32(buf + 2, file_size);
    write_u32(buf + 10, 54);

    // DIB Header
    write_u32(buf + 14, 40);
    write_u32(buf + 18, WIDTH);
    write_u32(buf + 22, HEIGHT); // Positive = bottom-up
    write_u16(buf + 26, 1);
    write_u16(buf + 28, 24);
    write_u32(buf + 30, 0);
    write_u32(buf + 34, image_size);

    uint8_t *pixels = buf + 54;

    // Fill background with Dark Slate (#0F141C)
    for (int y = 0; y < HEIGHT; y++) {
        for (int x = 0; x < WIDTH; x++) {
            uint8_t *p = pixels + (HEIGHT - 1 - y) * row_bytes + x * 3;
            p[0] = 0x1C; // B
            p[1] = 0x14; // G
            p[2] = 0x0F; // R
        }
    }

    // Draw Installer Window (x: 20..300, y: 15..165)
    for (int y = 15; y < 165; y++) {
        for (int x = 20; x < 300; x++) {
            uint8_t *p = pixels + (HEIGHT - 1 - y) * row_bytes + x * 3;
            if (y < 35) {
                // Title bar (#1E2638)
                p[0] = 0x38; p[1] = 0x26; p[2] = 0x1E;
            } else if (x == 20 || x == 299 || y == 15 || y == 164) {
                // Border (#3A4866)
                p[0] = 0x66; p[1] = 0x48; p[2] = 0x3A;
            } else {
                // Window body (#121722)
                p[0] = 0x22; p[1] = 0x17; p[2] = 0x12;
            }
        }
    }

    // Draw Progress Bar at y: 80..95, x: 40..280 (96% filled)
    uint32_t fill_x = 40 + (240 * 96) / 100;
    for (int y = 80; y < 95; y++) {
        for (int x = 40; x < 280; x++) {
            uint8_t *p = pixels + (HEIGHT - 1 - y) * row_bytes + x * 3;
            if (x <= (int)fill_x) {
                // Cyan accent (#00D2FF)
                p[0] = 0xFF; p[1] = 0xD2; p[2] = 0x00;
            } else {
                // Dark track (#202838)
                p[0] = 0x38; p[1] = 0x28; p[2] = 0x20;
            }
        }
    }

    FILE *f = fopen(out_path, "wb");
    if (!f) {
        free(buf);
        return 1;
    }
    fwrite(buf, 1, file_size, f);
    fclose(f);
    free(buf);

    printf("Generated demo screenshot: %s (%dx%d 24bpp)\n", out_path, WIDTH, HEIGHT);
    return 0;
}
