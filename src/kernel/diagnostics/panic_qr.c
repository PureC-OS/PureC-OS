#include "panic_qr.h"
#include "../../drivers/display/gop.h"
#include "boot_diag.h"
#include "build_stamp.h"
#include "klog.h"
#include "ksym.h"
#include "qr.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PQR_URL_MAX 2200
#define PQR_BODY_MAX 1600
#define PQR_FRAMES_MAX 8

static char pqr_url[PQR_URL_MAX];
static char pqr_body[PQR_BODY_MAX];
static uint8_t pqr_cells[QR_MAX_DIM * QR_MAX_DIM];
static int pqr_pos;

static void pqr_reset(void) {
  pqr_pos = 0;
}

static void pqr_c(char *dst, int cap, char c) {
  if (pqr_pos + 1 < cap) {
    dst[pqr_pos++] = c;
    dst[pqr_pos] = 0;
  }
}

static void pqr_s(char *dst, int cap, const char *s) {
  if (!s)
    s = "?";
  while (*s)
    pqr_c(dst, cap, *s++);
}

static void pqr_u(char *dst, int cap, unsigned int v) {
  char tmp[12];
  int n = 0;
  if (!v)
    tmp[n++] = '0';
  while (v > 0) {
    tmp[n++] = (char)('0' + v % 10);
    v /= 10;
  }
  while (n > 0)
    pqr_c(dst, cap, tmp[--n]);
}

static void pqr_hex(char *dst, int cap, uint64_t v, int width) {
  char tmp[16];
  int n = 0;
  if (!v)
    tmp[n++] = '0';
  while (v > 0) {
    unsigned int d = (unsigned int)(v & 0xF);
    tmp[n++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
    v >>= 4;
  }
  while (n < width)
    pqr_c(dst, cap, '0');
  while (n > 0)
    pqr_c(dst, cap, tmp[--n]);
}

static int pqr_unreserved(char c) {
  if (c >= 'A' && c <= 'Z')
    return 1;
  if (c >= 'a' && c <= 'z')
    return 1;
  if (c >= '0' && c <= '9')
    return 1;
  return c == '-' || c == '_' || c == '.' || c == '~';
}

static void pqr_hexbyte(char *dst, int cap, unsigned int v) {
  const char *h = "0123456789ABCDEF";
  pqr_c(dst, cap, h[(v >> 4) & 0xF]);
  pqr_c(dst, cap, h[v & 0xF]);
}

static void pqr_enc(char *dst, int cap, const char *s) {
  if (!s)
    s = "?";
  while (*s) {
    char c = *s++;
    if (pqr_unreserved(c))
      pqr_c(dst, cap, c);
    else if (c == ' ')
      pqr_s(dst, cap, "%20");
    else if (c == '\n')
      pqr_s(dst, cap, "%0A");
    else {
      pqr_c(dst, cap, '%');
      pqr_hexbyte(dst, cap, (unsigned int)(uint8_t)c);
    }
  }
}

static int pqr_kernel_addr(uint64_t a) {
  return a >= 0xffff800000000000ULL;
}

static void pqr_sym(char *dst, int cap, uint64_t addr) {
  const char *name = 0;
  uint64_t off = 0;
  pqr_hex(dst, cap, addr, 16);
  if (ksym_resolve(addr, &name, &off)) {
    pqr_c(dst, cap, ' ');
    pqr_s(dst, cap, name);
    pqr_c(dst, cap, '+');
    pqr_s(dst, cap, "0x");
    pqr_hex(dst, cap, off, 0);
  }
}

static void pqr_build_body(const char *reason, const char *file, int line,
                           const char *func, uint64_t rip, uint64_t rbp,
                           int frames) {
  pqr_pos = 0;
  pqr_body[0] = 0;
  pqr_s(pqr_body, PQR_BODY_MAX, "Build ");
  pqr_s(pqr_body, PQR_BODY_MAX, kernel_build_uuid);
  pqr_c(pqr_body, PQR_BODY_MAX, ' ');
  pqr_s(pqr_body, PQR_BODY_MAX, kernel_build_time);
  pqr_c(pqr_body, PQR_BODY_MAX, '\n');
  pqr_s(pqr_body, PQR_BODY_MAX, reason ? reason : "unknown fatal error");
  pqr_c(pqr_body, PQR_BODY_MAX, '\n');
  pqr_s(pqr_body, PQR_BODY_MAX, "at ");
  pqr_s(pqr_body, PQR_BODY_MAX, file ? file : "?");
  pqr_c(pqr_body, PQR_BODY_MAX, ':');
  pqr_u(pqr_body, PQR_BODY_MAX, line < 0 ? 0 : (unsigned int)line);
  pqr_c(pqr_body, PQR_BODY_MAX, ' ');
  pqr_s(pqr_body, PQR_BODY_MAX, func ? func : "?");
  pqr_s(pqr_body, PQR_BODY_MAX, "()\n");
  pqr_s(pqr_body, PQR_BODY_MAX, "RIP ");
  if (rip)
    pqr_sym(pqr_body, PQR_BODY_MAX, rip);
  else
    pqr_s(pqr_body, PQR_BODY_MAX, "-");
  pqr_c(pqr_body, PQR_BODY_MAX, '\n');
  int depth = 0;
  int printed = 0;
  while (printed < frames && depth < 24 && pqr_kernel_addr(rbp)) {
    volatile uint64_t *frame = (volatile uint64_t *)(uintptr_t)rbp;
    uint64_t next_rbp = frame[0];
    uint64_t ret_addr = frame[1];
    if (ret_addr >= 0xffffffff80000000ULL) {
      pqr_c(pqr_body, PQR_BODY_MAX, '#');
      pqr_u(pqr_body, PQR_BODY_MAX, (unsigned int)printed);
      pqr_c(pqr_body, PQR_BODY_MAX, ' ');
      pqr_sym(pqr_body, PQR_BODY_MAX, ret_addr);
      pqr_c(pqr_body, PQR_BODY_MAX, '\n');
      printed++;
    }
    if (next_rbp == 0 || next_rbp <= rbp)
      break;
    if (!pqr_kernel_addr(next_rbp))
      break;
    rbp = next_rbp;
    depth++;
  }
  pqr_s(pqr_body, PQR_BODY_MAX, "stage ");
  pqr_u(pqr_body, PQR_BODY_MAX, (unsigned int)boot_diag_current_stage());
  pqr_c(pqr_body, PQR_BODY_MAX, ' ');
  pqr_s(pqr_body, PQR_BODY_MAX, boot_diag_current_detail());
  pqr_c(pqr_body, PQR_BODY_MAX, '\n');
}

static int pqr_build_url(const char *reason, const char *file, int line,
                         const char *func, uint64_t rip, uint64_t rbp) {
  int frames = PQR_FRAMES_MAX;
  while (frames >= 0) {
    pqr_build_body(reason, file, line, func, rip, rbp, frames);
    pqr_reset();
    pqr_url[0] = 0;
    pqr_s(pqr_url, PQR_URL_MAX,
          "https://github.com/PureC-OS/PureC-OS/issues/new?title=");
    pqr_enc(pqr_url, PQR_URL_MAX, "Kernel panic ");
    pqr_enc(pqr_url, PQR_URL_MAX, kernel_build_uuid);
    pqr_s(pqr_url, PQR_URL_MAX, "&body=");
    pqr_enc(pqr_url, PQR_URL_MAX, pqr_body);
    if (pqr_pos <= 1200)
      return pqr_pos;
    if (frames == 0)
      break;
    frames /= 2;
  }
  return 0;
}

static void pqr_draw(int size) {
  uint32_t w = gop_get_width();
  uint32_t h = gop_get_height();
  int need = size + 8;
  int scale = 1;
  while ((need * (scale + 1) <= (int)w) && (need * (scale + 1) * 2 <= (int)h))
    scale++;
  if (need * scale > (int)w || need * scale > (int)h)
    return;
  int dim = need * scale;
  int x0 = ((int)w - dim) / 2;
  if (x0 < 0)
    x0 = 0;
  int y0 = (int)h - dim - 16;
  if (y0 < 0)
    y0 = 0;
  gop_draw_rect((uint32_t)x0, (uint32_t)y0, (uint32_t)dim, (uint32_t)dim, 0xFFFFFF);
  for (int y = 0; y < size; y++) {
    for (int x = 0; x < size; x++) {
      if (!pqr_cells[y * QR_MAX_DIM + x])
        continue;
      gop_draw_rect((uint32_t)(x0 + (x + 4) * scale),
                    (uint32_t)(y0 + (y + 4) * scale), (uint32_t)scale,
                    (uint32_t)scale, 0x000000);
    }
  }
}

void panic_qr_show(const char *reason, const char *file, int line,
                   const char *func, uint64_t rip, uint64_t rbp) {
  if (!gop_is_available())
    return;
  int len = pqr_build_url(reason, file, line, func, rip, rbp);
  if (len <= 0) {
    klog(KLOG_ERROR, "QR: issue URL does not fit, skipped");
    return;
  }
  int size = 0;
  if (!qr_encode((const uint8_t *)pqr_url, (unsigned int)len, pqr_cells, &size)) {
    klog(KLOG_ERROR, "QR: encode failed, skipped");
    return;
  }
  klog(KLOG_ERROR, "Scan QR to open a prefilled GitHub issue:");
  pqr_draw(size);
}
