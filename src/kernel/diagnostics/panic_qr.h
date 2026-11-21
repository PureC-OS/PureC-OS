#pragma once
#include <stdint.h>

void panic_qr_show(const char *reason, const char *file, int line,
                   const char *func, uint64_t rip, uint64_t rbp);
void panic_qr_set_box(uint32_t x, uint32_t y, uint32_t dim, uint32_t frame);
