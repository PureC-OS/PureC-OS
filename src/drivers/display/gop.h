#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "../../boot/limine.h"

// Универсальный GOP/Framebuffer драйвер
// Работает через Limine (UEFI) и как fallback через VGA text (BIOS)
struct gop_state {
    uint32_t *addr;
    uint32_t width, height, pitch;
    uint64_t framebuffer_bytes;
    const char *protocol_name;
    uint8_t bpp;
    bool available;
};

enum gop_font_face {
    GOP_FONT_CLASSIC,
    GOP_FONT_CLEAN,
    GOP_FONT_BOLD
};

void gop_init_from_limine(struct limine_framebuffer *fb, uint64_t firmware_type);
void gop_init_from_multiboot(void *mbi); // парсит multiboot2 info
bool gop_is_available(void);
uint32_t gop_get_width(void);
uint32_t gop_get_height(void);
uint32_t gop_get_pitch(void);
uint8_t gop_get_bpp(void);
uint64_t gop_get_framebuffer_size_bytes(void);
const char *gop_get_protocol_name(void);
void gop_set_font_face(enum gop_font_face face);
enum gop_font_face gop_get_font_face(void);
void gop_clear(uint32_t color);
void gop_set_color(uint32_t fg, uint32_t bg);
void gop_putc(char c);
void gop_write(const char *s);
void gop_write_hex(uint64_t v);
bool gop_console_configure(uint32_t x, uint32_t y,
                           uint32_t width, uint32_t height,
                           uint32_t foreground, uint32_t background);
bool gop_console_is_active(void);
void gop_console_putc(char character);
void gop_console_clear(void);
void gop_console_disable(void);
void gop_draw_text_at(uint32_t x, uint32_t y, const char *text, uint32_t fg, uint32_t bg);
void gop_draw_text_sized_at(uint32_t x, uint32_t y, const char *text,
                            uint32_t fg, uint32_t bg, uint32_t size);
uint32_t gop_get_pixel(uint32_t x, uint32_t y);
void gop_put_pixel(uint32_t x, uint32_t y, uint32_t color);
void gop_draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void gop_scroll_rect_up(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                        uint32_t amount, uint32_t fill_color);
void gop_draw_line(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, uint32_t color);
void gop_begin_batch(void);
void gop_end_batch(void);
void gop_present(void);
bool gop_has_backbuffer(void);
void gop_copy_back_to_front(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
void gop_put_pixel_front(uint32_t x, uint32_t y, uint32_t color);
