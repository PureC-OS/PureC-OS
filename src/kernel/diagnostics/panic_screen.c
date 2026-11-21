#include "panic_screen.h"
#include "../../drivers/display/gop.h"
#include "panic_qr.h"
#include <stddef.h>

extern const uint8_t vt_font_8x16[256][16];

#define FG 0xE9E9F0
#define DIM 0x9C9CB0

#define ARITH(code, name, hint) {0x2B1606, 0xFF9F43, 0x1A0D00, 0xFFC285, code, name, hint}
#define DEBUG(code, name, hint) {0x06221F, 0x2ED3B7, 0x002420, 0x7FF0DC, code, name, hint}
#define SEGMENT(code, name, hint) {0x1F1B08, 0xD4B83A, 0x1A1600, 0xF0DC80, code, name, hint}
#define VIRT(code, name, hint) {0x0F0F33, 0x6C6CFF, 0x08082A, 0xB0B0FF, code, name, hint}
#define RESERVED(code) {0x161616, 0x9E9E9E, 0x101010, 0xD0D0D0, code, "Reserved Exception", "The CPU raised a vector that Intel marks as reserved."}

static const struct panic_theme vector_themes[32] = {
    ARITH("#DE", "Divide Error", "Integer division by zero or a quotient too large for the destination."),
    DEBUG("#DB", "Debug Exception", "Unexpected debug trap: hardware breakpoint, watchpoint or single step."),
    {0x1A0E2B, 0xB388FF, 0x140A24, 0xD1B8FF, "NMI", "Non-Maskable Interrupt", "Hardware signalled a fatal condition: memory parity, bus error or a watchdog."},
    DEBUG("#BP", "Breakpoint", "An int3 instruction was hit in kernel code outside the IDT self-test."),
    ARITH("#OF", "Overflow", "INTO executed with the overflow flag set."),
    ARITH("#BR", "Bound Range Exceeded", "BOUND found an index outside the array limits."),
    {0x2A0726, 0xF35AD5, 0x22001E, 0xFF9BEA, "#UD", "Invalid Opcode", "The CPU met an instruction it cannot run: corrupted code, a wild jump or a missing CPU feature."},
    {0x12181F, 0x8DA2B5, 0x0A1016, 0xC2D0DC, "#NM", "Device Not Available", "FPU or SSE was used while CR0.TS was set: the lazy FPU switch failed."},
    {0x2A0000, 0xFF2D2D, 0x200000, 0xFF8A8A, "#DF", "Double Fault", "A fault happened while the CPU was delivering another one. Most often a kernel stack overflow."},
    SEGMENT("#CSO", "Coprocessor Segment Overrun", "Legacy x87 exception that modern CPUs never raise."),
    SEGMENT("#TS", "Invalid TSS", "The task state segment is broken: check the GDT and TSS setup."),
    SEGMENT("#NP", "Segment Not Present", "A segment or gate marked as not present was loaded."),
    SEGMENT("#SS", "Stack-Segment Fault", "Stack access out of bounds or with a non-canonical stack pointer."),
    {0x08163A, 0x4C8DFF, 0x04102A, 0x9CC2FF, "#GP", "General Protection Fault", "Privileged instruction, non-canonical address or a bad segment selector."},
    {0x2A0814, 0xFF4D7A, 0x22000C, 0xFF9AB4, "#PF", "Page Fault", "Access to an unmapped page, or to a page without the needed rights."},
    RESERVED("#15"),
    ARITH("#MF", "x87 Floating-Point Error", "An unmasked x87 floating point exception was pending."),
    {0x18220A, 0xA6E22E, 0x101800, 0xD2F58C, "#AC", "Alignment Check", "Misaligned memory access while alignment checking was on."},
    {0x201000, 0xFFC400, 0x1A1000, 0xFFE07A, "#MC", "Machine Check", "The CPU reported a hardware error. Check RAM, temperature and power supply."},
    ARITH("#XM", "SIMD Floating-Point Error", "An unmasked SSE floating point exception was raised."),
    VIRT("#VE", "Virtualization Exception", "EPT violation reported to the guest."),
    VIRT("#CP", "Control Protection", "CET violation: shadow stack mismatch or a missing ENDBR at a branch target."),
    RESERVED("#22"),
    RESERVED("#23"),
    RESERVED("#24"),
    RESERVED("#25"),
    RESERVED("#26"),
    RESERVED("#27"),
    VIRT("#HV", "Hypervisor Injection", "The hypervisor injected an exception into the guest."),
    VIRT("#VC", "VMM Communication", "SEV-ES guest needs the hypervisor to handle an instruction."),
    VIRT("#SX", "Security Exception", "SVM security event, such as an INIT redirection."),
    RESERVED("#31"),
};

static const struct panic_theme software_theme = {
    0x14121C, 0xF38BA8, 0x1E0A12, 0xF7B6C8, "PANIC", "Kernel Panic",
    "Kernel code found a state it cannot recover from and stopped the machine."};
static const struct panic_theme manual_theme = {
    0x15140E, 0xF9E2AF, 0x1C1704, 0xFBEDC9, "BUG", "Kernel Bug",
    "A KERNEL_PANIC_HERE check failed in kernel code."};
static const struct panic_theme unknown_theme = {
    0x161616, 0x9E9E9E, 0x101010, 0xD0D0D0, "INT", "Unexpected Interrupt",
    "An interrupt vector without a handler reached the exception path."};

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

static const struct panic_theme *theme;
static bool active;
static uint32_t screen_w, screen_h, margin, body_scale;
static uint32_t col_x, col_y, col_w, col_top, col_bottom, col_right;
static bool body_full;

static void glyph(uint32_t x, uint32_t y, unsigned char c, uint32_t s, uint32_t color)
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
      if (s == 1)
        gop_put_pixel(x + rx, y + ry, color);
      else
        gop_draw_rect(x + rx * s, y + ry * s, s, s, color);
    }
  }
}

static uint32_t text(uint32_t x, uint32_t y, const char *s, uint32_t scale,
                     uint32_t color, uint32_t max_w)
{
  uint32_t cw = 8 * scale;
  uint32_t limit = max_w / cw;
  uint32_t n = 0;
  for (; s && *s && n < limit; s++, n++)
    glyph(x + n * cw, y, (unsigned char)*s, scale, color);
  return n * cw;
}

static uint32_t line_h(void)
{
  return 18 * body_scale;
}

static bool next_column(void)
{
  uint32_t gap = margin;
  uint32_t nx = col_x + col_w + gap;
  if (nx + col_w > col_right)
    return false;
  col_x = nx;
  col_y = col_top;
  return true;
}

static bool reserve_line(uint32_t h)
{
  if (body_full)
    return false;
  if (col_y + h > col_bottom && !next_column())
  {
    body_full = true;
    return false;
  }
  return true;
}

void pscr_begin(const struct panic_theme *t, const char *title)
{
  active = false;
  if (!gop_is_available())
    return;
  theme = t;
  screen_w = gop_get_width();
  screen_h = gop_get_height();
  if (screen_w < 320 || screen_h < 240)
    return;
  active = true;
  body_full = false;
  gop_begin_batch();
  gop_clear(t->bg);

  margin = screen_w >= 1600 ? 40 : 20;
  body_scale = screen_w >= 3000 ? 2 : 1;
  uint32_t title_scale = screen_w >= 1600 ? 3 : 2;
  uint32_t band_h = margin + 16 + 10 + 16 * title_scale + 10 + 16 + margin * 3 / 4;
  gop_draw_rect(0, 0, screen_w, band_h, t->band);
  gop_draw_rect(0, band_h, screen_w, 4, t->accent);

  uint32_t x = margin, y = margin;
  uint32_t w = screen_w - 2 * margin;
  uint32_t used = text(x, y, "PureC OS", 1, t->band_fg, w);
  text(x + used + 16, y, "KERNEL PANIC  -  the system has been stopped", 1, t->band_fg, w - used - 16);
  y += 16 + 10;

  uint32_t code_len = 0;
  while (t->code[code_len])
    code_len++;
  uint32_t badge_w = code_len * 8 * title_scale;
  gop_draw_rect(x - 8, y - 4, badge_w + 16, 16 * title_scale + 8, t->bg);
  text(x, y, t->code, title_scale, t->accent, w);
  uint32_t tx = x + badge_w + 8 + 8 * title_scale;
  text(tx, y, title && title[0] ? title : t->name, title_scale, t->band_fg, screen_w - margin - tx);
  y += 16 * title_scale + 10;
  text(x, y, t->hint, 1, t->band_fg, w);

  uint32_t footer_h = 16 + margin / 2;
  col_top = band_h + 4 + margin;
  col_bottom = screen_h - margin - footer_h;
  col_w = 8 * body_scale * 66;
  col_x = margin;
  col_y = col_top;
  uint32_t qr_dim = screen_h / 3;
  if (qr_dim > 360)
    qr_dim = 360;
  if (qr_dim + 2 * margin > screen_w / 3)
    qr_dim = screen_w / 3 > 2 * margin ? screen_w / 3 - 2 * margin : 0;
  col_right = screen_w - margin - (qr_dim ? qr_dim + margin : 0);
  if (col_w > col_right - margin)
    col_w = col_right - margin;
  if (qr_dim)
    panic_qr_set_box(screen_w - margin - qr_dim, col_bottom - qr_dim, qr_dim, theme->accent);
}

bool pscr_active(void)
{
  return active;
}

void pscr_section(const char *title)
{
  if (!active)
    return;
  uint32_t h = line_h();
  if (col_y != col_top)
    col_y += h / 2;
  if (!reserve_line(h * 2))
    return;
  text(col_x, col_y, title, body_scale, theme->accent, col_w);
  gop_draw_rect(col_x, col_y + 16 * body_scale + 2, col_w, 1, theme->accent);
  col_y += h + 4;
}

void pscr_line(enum pscr_tone tone, const char *s)
{
  if (!active || !reserve_line(line_h()))
    return;
  uint32_t color = tone == PSCR_DIM ? DIM : tone == PSCR_ACCENT ? theme->accent : FG;
  text(col_x, col_y, s, body_scale, color, col_w);
  col_y += line_h();
}

void pscr_gap(void)
{
  if (active && col_y != col_top)
    col_y += line_h() / 2;
}

void pscr_footer(const char *s)
{
  if (!active)
    return;
  uint32_t y = screen_h - margin / 2 - 16;
  gop_draw_rect(0, y - margin / 4, screen_w, 1, theme->accent);
  text(margin, y, s, 1, DIM, screen_w - 2 * margin);
}
