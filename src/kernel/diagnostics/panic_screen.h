#pragma once
#include <stdbool.h>
#include <stdint.h>

#define PANIC_KIND_SOFTWARE (-1)
#define PANIC_KIND_MANUAL (-2)

enum pscr_tone
{
  PSCR_TEXT,
  PSCR_DIM,
  PSCR_ACCENT,
  PSCR_HEAD
};

struct panic_theme
{
  uint32_t bg;
  uint32_t band;
  uint32_t band_fg;
  uint32_t accent;
  const char *code;
  const char *name;
  const char *hint;
};

const struct panic_theme *panic_theme_for(int64_t kind);
void pscr_begin(const struct panic_theme *theme, const char *title);
void pscr_section(const char *title);
void pscr_line(enum pscr_tone tone, const char *text);
void pscr_gap(void);
void pscr_footer(const char *text);
bool pscr_active(void);
