#pragma once
#include <stdbool.h>
#include <stdint.h>

#define PANIC_KIND_SOFTWARE (-1)
#define PANIC_KIND_MANUAL (-2)

struct panic_theme
{
  uint32_t bg;
  const char *code;
  const char *name;
};

const struct panic_theme *panic_theme_for(int64_t kind);
void pscr_begin(const struct panic_theme *theme);
void pscr_line(const char *text);
bool pscr_active(void);
