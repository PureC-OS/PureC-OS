#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

enum display_font_face {
    DISPLAY_FONT_CLASSIC,
    DISPLAY_FONT_CLEAN,
    DISPLAY_FONT_BOLD
};

struct display_info {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint64_t size_bytes;
    uint8_t bpp;
    bool available;
    char protocol_name[16];
};

bool display_get_info(struct display_info *info);
bool display_is_available(void);
uint32_t display_get_width(void);
uint32_t display_get_height(void);
uint8_t display_get_bpp(void);
uint64_t display_get_framebuffer_size_bytes(void);
const char *display_get_protocol_name(void);
void display_set_font_face(enum display_font_face face);
enum display_font_face display_get_font_face(void);
void display_clear(uint32_t color);
void display_draw_text_at(uint32_t x, uint32_t y, const char *text,
                          uint32_t fg, uint32_t bg);
void display_draw_text_sized_at(uint32_t x, uint32_t y, const char *text,
                                uint32_t fg, uint32_t bg, uint32_t size);
void display_draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                       uint32_t color);
void display_scroll_rect_up(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                            uint32_t amount, uint32_t fill_color);
void display_draw_line(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1,
                       uint32_t color);

#ifdef __cplusplus
}
#endif
