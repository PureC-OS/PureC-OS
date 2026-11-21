#include "panic_screen.h"
#include "../../drivers/display/gop.h"
#include "panic_qr.h"
#include <stddef.h>

extern const uint8_t vt_font_8x16[256][16];

#define TEXT_COLOR 0xFFFFFF
#define TEXT_COLUMNS 100

static const struct panic_theme vector_themes[32] = {
    {0x7A3A00, "#DE", "Divide Error"},
    {0x00505A, "#DB", "Debug"},
    {0x2E1A5E, "NMI", "Non-Maskable Interrupt"},
    {0x00505A, "#BP", "Breakpoint"},
    {0x7A3A00, "#OF", "Overflow"},
    {0x7A3A00, "#BR", "Bound Range Exceeded"},
    {0x6E1060, "#UD", "Invalid Opcode"},
    {0x2F3B48, "#NM", "Device Not Available"},
    {0x8B0000, "#DF", "Double Fault"},
    {0x4A4020, "#CSO", "Coprocessor Segment Overrun"},
    {0x4A4020, "#TS", "Invalid TSS"},
    {0x4A4020, "#NP", "Segment Not Present"},
    {0x4A4020, "#SS", "Stack-Segment Fault"},
    {0x6B4600, "#GP", "General Protection"},
    {0x4A2A6E, "#PF", "Page Fault"},
    {0x333333, "#15", "Reserved"},
    {0x7A3A00, "#MF", "x87 Floating-Point Error"},
    {0x2E5A10, "#AC", "Alignment Check"},
    {0x5C5000, "#MC", "Machine Check"},
    {0x7A3A00, "#XM", "SIMD Floating-Point Error"},
    {0x1E1E6E, "#VE", "Virtualization Exception"},
    {0x1E1E6E, "#CP", "Control Protection"},
    {0x333333, "#22", "Reserved"},
    {0x333333, "#23", "Reserved"},
    {0x333333, "#24", "Reserved"},
    {0x333333, "#25", "Reserved"},
    {0x333333, "#26", "Reserved"},
    {0x333333, "#27", "Reserved"},
    {0x1E1E6E, "#HV", "Hypervisor Injection"},
    {0x1E1E6E, "#VC", "VMM Communication"},
    {0x1E1E6E, "#SX", "Security Exception"},
    {0x333333, "#31", "Reserved"},
};

static const struct panic_theme software_theme = {0x000000, "PANIC", "Kernel panic"};
static const struct panic_theme manual_theme = {0x101010, "BUG", "Kernel bug"};
static const struct panic_theme unknown_theme = {0x333333, "INT", "Unexpected interrupt"};

const struct panic_theme *panic_theme_for(int64_t kind)
{
  if (kind == PANIC_KIND_SOFTWARE)
    return &software_theme;
  if (kind == PANIC_KIND_MANUAL)
    return &manual_theme;
  if (kind >= 0 && kind < 32)
    return &vector_themes[kind];
  return &unknown_theme;
}

static bool active;
static uint32_t screen_w, screen_h, margin, scale;
static uint32_t col_x, col_y, col_w, col_top, col_bottom, col_right;
static bool full;

static void glyph(uint32_t x, uint32_t y, unsigned char c)
{
  const uint8_t *rows = vt_font_8x16[c];
  for (uint32_t ry = 0; ry < 16; ry++)
  {
    uint8_t bits = rows[ry];
    if (!bits)
      continue;
    for (uint32_t rx = 0; rx < 8; rx++)
    {
      if (!(bits & (0x80 >> rx)))
        continue;
      if (scale == 1)
        gop_put_pixel(x + rx, y + ry, TEXT_COLOR);
      else
        gop_draw_rect(x + rx * scale, y + ry * scale, scale, scale, TEXT_COLOR);
    }
  }
}

void pscr_begin(const struct panic_theme *theme)
{
  active = false;
  if (!gop_is_available())
    return;
  screen_w = gop_get_width();
  screen_h = gop_get_height();
  if (screen_w < 320 || screen_h < 200)
    return;
  active = true;
  full = false;
  gop_begin_batch();
  gop_clear(theme->bg);
  scale = screen_w >= 3000 ? 2 : 1;
  margin = 16 * scale;
  col_w = TEXT_COLUMNS * 8 * scale;
  col_top = margin;
  col_bottom = screen_h - margin;
  col_x = margin;
  col_y = col_top;

  uint32_t room_w = screen_w > col_w + 3 * margin ? screen_w - col_w - 3 * margin : 0;
  uint32_t dim = screen_h - 2 * margin;
  if (dim > room_w)
    dim = room_w;
  if (dim > 520 * scale)
    dim = 520 * scale;
  if (dim >= 160)
  {
    col_right = screen_w - dim - 2 * margin;
    panic_qr_set_box(screen_w - margin - dim, (screen_h - dim) / 2, dim, TEXT_COLOR);
  }
  else
  {
    col_right = screen_w - margin;
    if (col_w > col_right - margin)
      col_w = col_right - margin;
  }
}

bool pscr_active(void)
{
  return active;
}

void pscr_line(const char *s)
{
  if (!active || full)
    return;
  uint32_t h = 16 * scale;
  if (col_y + h > col_bottom)
  {
    uint32_t nx = col_x + col_w + margin;
    if (nx + col_w > col_right)
    {
      full = true;
      return;
    }
    col_x = nx;
    col_y = col_top;
  }
  uint32_t cw = 8 * scale;
  uint32_t limit = col_w / cw;
  for (uint32_t n = 0; s && s[n] && n < limit; n++)
    if (s[n] != ' ')
      glyph(col_x + n * cw, col_y, (unsigned char)s[n]);
  col_y += h;
}
