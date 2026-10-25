#pragma once
#include <stdint.h>

void panic_qr_show(const char *reason, const char *file, int line,
                   const char *func, uint64_t rip, uint64_t rbp);
