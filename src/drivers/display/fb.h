#pragma once
#include "../../boot/limine.h"
#include <stdint.h>

void fb_init(struct limine_framebuffer *fb);
void fb_clear(uint32_t color);
void fb_set_color(uint32_t fg, uint32_t bg);
void fb_putc(char c);
void fb_write_string(const char *s);
void fb_write_hex(uint64_t v);
