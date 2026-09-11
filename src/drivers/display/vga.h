#pragma once
#include <stdint.h>
void vga_init(void);
void vga_set_hhdm(uint64_t offset);
void vga_putc(char c);
void vga_write(const char *s);
void vga_clear(void);
