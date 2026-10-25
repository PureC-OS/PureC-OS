#include "qr.h"

static const uint16_t qr_data_cw[26] = {
    0,  19,  34,  55,  80,  108, 136, 156, 194, 232,
    274, 324, 370, 428, 461, 523, 589, 647, 721, 795,
    861, 932, 1006, 1094, 1174, 1276,
};
static const uint8_t qr_ec_cw[26] = {
    0, 7, 10, 15, 20, 26, 18, 20, 24, 30,
    18, 20, 24, 26, 30, 22, 24, 28, 30, 28,
    28, 28, 28, 30, 30, 26,
};
static const uint8_t qr_g1_n[26] = {
    0, 1, 1, 1, 1, 1, 2, 2, 2, 2,
    2, 4, 2, 4, 3, 5, 5, 1, 5, 3,
    3, 4, 2, 4, 6, 8,
};
static const uint8_t qr_g1_d[26] = {
    0,  19,  34,  55,  80,  108, 68,  78,  97,  116,
    68, 81,  92,  107, 115, 87,  98,  107, 120, 113,
    107, 116, 111, 121, 117, 106,
};
static const uint8_t qr_g2_n[26] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    2, 0, 2, 0, 1, 1, 1, 5, 1, 4,
    5, 4, 7, 5, 4, 4,
};
static const uint8_t qr_g2_d[26] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    69, 0, 93, 0, 116, 88, 99, 108, 121, 114,
    108, 117, 112, 122, 118, 107,
};
static const uint8_t qr_align[26][6] = {
    {0, 0, 0, 0, 0, 0},   {0, 0, 0, 0, 0, 0},   {2, 6, 18, 0, 0, 0},
    {2, 6, 22, 0, 0, 0},  {2, 6, 26, 0, 0, 0},  {2, 6, 30, 0, 0, 0},
    {2, 6, 34, 0, 0, 0},  {3, 6, 22, 38, 0, 0}, {3, 6, 24, 42, 0, 0},
    {3, 6, 26, 46, 0, 0}, {3, 6, 28, 50, 0, 0}, {3, 6, 30, 54, 0, 0},
    {3, 6, 32, 58, 0, 0}, {3, 6, 34, 62, 0, 0}, {4, 6, 26, 46, 66, 0},
    {4, 6, 26, 48, 70, 0}, {4, 6, 26, 50, 74, 0}, {4, 6, 30, 54, 78, 0},
    {4, 6, 30, 56, 82, 0}, {4, 6, 30, 58, 86, 0}, {4, 6, 34, 62, 90, 0},
    {5, 6, 28, 50, 72, 94}, {5, 6, 26, 50, 74, 98}, {5, 6, 30, 54, 78, 102},
    {5, 6, 28, 54, 80, 106}, {5, 6, 32, 58, 84, 110},
};

static uint8_t q_cells[QR_MAX_DIM * QR_MAX_DIM];
static uint8_t q_func[QR_MAX_DIM * QR_MAX_DIM];
static uint8_t q_data[1300];
static uint8_t q_msg[1600];
static uint8_t q_gen[32];
static uint8_t q_poly[32];
static uint8_t q_blk[160];
static uint8_t q_ecb[16 * 32];
static uint8_t q_exp[512];
static uint8_t q_log[256];
static uint8_t *q_wp;
static int q_wpos;
static int q_n;

static void q_gf_init(void) {
  unsigned int x = 1;
  for (int i = 0; i < 255; i++) {
    q_exp[i] = (uint8_t)x;
    q_log[x] = (uint8_t)i;
    x <<= 1;
    if (x & 0x100)
      x ^= 0x11D;
  }
  for (int i = 255; i < 512; i++)
    q_exp[i] = q_exp[i - 255];
}

static uint8_t q_gf_mul(uint8_t a, uint8_t b) {
  if (!a || !b)
    return 0;
  return q_exp[q_log[a] + q_log[b]];
}

static void q_rs_gen(int ec) {
  q_poly[0] = 1;
  int deg = 0;
  for (int i = 0; i < ec; i++) {
    uint8_t f = q_exp[i];
    q_poly[deg + 1] = 0;
    for (int j = deg; j >= 0; j--) {
      uint8_t v = q_poly[j];
      q_poly[j + 1] ^= v;
      q_poly[j] = v ? q_gf_mul(v, f) : 0;
    }
    deg++;
  }
  for (int j = 0; j <= ec; j++)
    q_gen[j] = q_poly[j];
}

static void q_rs_rem(const uint8_t *data, int len, int ec, uint8_t *out) {
  for (int i = 0; i < len + ec; i++)
    q_blk[i] = i < len ? data[i] : 0;
  for (int i = 0; i < len; i++) {
    uint8_t f = q_blk[i];
    if (f) {
      for (int j = 0; j <= ec; j++)
        q_blk[i + j] ^= q_gf_mul(q_gen[ec - j], f);
    }
  }
  for (int i = 0; i < ec; i++)
    out[i] = q_blk[len + i];
}

static int q_fmt_bits(int mask) {
  int d = (1 << 3) | mask;
  int r = d;
  for (int i = 0; i < 10; i++)
    r = (r << 1) ^ ((r >> 9) * 0x537);
  return ((d << 10) | r) ^ 0x5412;
}

static int q_ver_bits(int v) {
  int r = v;
  for (int i = 0; i < 12; i++)
    r = (r << 1) ^ ((r >> 11) * 0x1F25);
  return (v << 12) | r;
}

static void q_set_f(int x, int y, uint8_t v) {
  if (x < 0 || y < 0 || x >= q_n || y >= q_n)
    return;
  q_cells[y * QR_MAX_DIM + x] = v;
  q_func[y * QR_MAX_DIM + x] = 1;
}

static uint8_t q_is_f(int x, int y) {
  return q_func[y * QR_MAX_DIM + x];
}

static void q_sep(int x, int y) {
  if (x < 0 || y < 0 || x >= q_n || y >= q_n)
    return;
  if (q_is_f(x, y))
    return;
  q_set_f(x, y, 0);
}

static void q_finder(int ox, int oy) {
  for (int d = -1; d <= 7; d++) {
    q_sep(ox + d, oy - 1);
    q_sep(ox + d, oy + 7);
    q_sep(ox - 1, oy + d);
    q_sep(ox + 7, oy + d);
  }
  for (int dy = 0; dy < 7; dy++) {
    for (int dx = 0; dx < 7; dx++) {
      uint8_t v = 0;
      if (dx == 0 || dx == 6 || dy == 0 || dy == 6 || (dx >= 2 && dx <= 4 && dy >= 2 && dy <= 4))
        v = 1;
      q_set_f(ox + dx, oy + dy, v);
    }
  }
}

static void q_align_pat(int cx, int cy) {
  for (int dy = -2; dy <= 2; dy++) {
    for (int dx = -2; dx <= 2; dx++) {
      int ax = dx < 0 ? -dx : dx;
      int ay = dy < 0 ? -dy : dy;
      if (ax < ay)
        ax = ay;
      q_set_f(cx + dx, cy + dy, ax != 1);
    }
  }
}

static void q_format(int bits) {
  for (int i = 0; i <= 5; i++)
    q_set_f(8, i, (bits >> i) & 1);
  q_set_f(8, 7, (bits >> 6) & 1);
  q_set_f(8, 8, (bits >> 7) & 1);
  q_set_f(7, 8, (bits >> 8) & 1);
  for (int i = 9; i < 15; i++)
    q_set_f(14 - i, 8, (bits >> i) & 1);
  for (int i = 0; i < 8; i++)
    q_set_f(q_n - 1 - i, 8, (bits >> i) & 1);
  for (int i = 8; i < 15; i++)
    q_set_f(8, q_n - 15 + i, (bits >> i) & 1);
  q_set_f(8, q_n - 8, 1);
}

static void q_version(int v) {
  if (v < 7)
    return;
  int bits = q_ver_bits(v);
  for (int i = 0; i < 18; i++) {
    uint8_t b = (bits >> i) & 1;
    int a = q_n - 11 + i % 3;
    q_set_f(a, i / 3, b);
    q_set_f(i / 3, a, b);
  }
}

static void q_place_data(const uint8_t *msg, int total_bits) {
  int bit = 0;
  for (int right = q_n - 1; right >= 1; right -= 2) {
    if (right == 6)
      right = 5;
    for (int vert = 0; vert < q_n; vert++) {
      for (int j = 0; j < 2; j++) {
        int x = right - j;
        int y = ((right + 1) & 2) == 0 ? q_n - 1 - vert : vert;
        if (q_is_f(x, y))
          continue;
        uint8_t v = 0;
        if (bit < total_bits)
          v = (msg[bit >> 3] >> (7 - (bit & 7))) & 1;
        q_cells[y * QR_MAX_DIM + x] = v;
        bit++;
      }
    }
  }
}

static int q_mask_cond(int m, int x, int y) {
  switch (m) {
  case 0:
    return (x + y) % 2 == 0;
  case 1:
    return y % 2 == 0;
  case 2:
    return x % 3 == 0;
  case 3:
    return (x + y) % 3 == 0;
  case 4:
    return (x / 3 + y / 2) % 2 == 0;
  case 5:
    return (x * y) % 2 + (x * y) % 3 == 0;
  case 6:
    return ((x * y) % 2 + (x * y) % 3) % 2 == 0;
  default:
    return ((x + y) % 2 + (x * y) % 3) % 2 == 0;
  }
}

static void q_apply_mask(int m) {
  for (int y = 0; y < q_n; y++) {
    for (int x = 0; x < q_n; x++) {
      if (!q_is_f(x, y) && q_mask_cond(m, x, y))
        q_cells[y * QR_MAX_DIM + x] ^= 1;
    }
  }
}

static int q_run_penalty(int horizontal) {
  int res = 0;
  for (int a = 0; a < q_n; a++) {
    int run = 1;
    for (int b = 0; b < q_n; b++) {
      uint8_t c0 = horizontal ? q_cells[a * QR_MAX_DIM + b] : q_cells[b * QR_MAX_DIM + a];
      uint8_t c1 = 2;
      if (b + 1 < q_n)
        c1 = horizontal ? q_cells[a * QR_MAX_DIM + b + 1] : q_cells[(b + 1) * QR_MAX_DIM + a];
      if (c0 == c1)
        run++;
      else {
        if (run >= 5)
          res += 3 + (run - 5);
        run = 1;
      }
    }
  }
  return res;
}

static int q_pat_penalty(int horizontal) {
  int res = 0;
  for (int a = 0; a < q_n; a++) {
    int pat = 0;
    for (int b = 0; b < q_n; b++) {
      uint8_t c = horizontal ? q_cells[a * QR_MAX_DIM + b] : q_cells[b * QR_MAX_DIM + a];
      pat = ((pat << 1) & 0x7FF) | c;
      if (b >= 10 && (pat == 0x5D0 || pat == 0x05D))
        res += 40;
    }
  }
  return res;
}

static int q_penalty(void) {
  int res = q_run_penalty(1) + q_run_penalty(0) + q_pat_penalty(1) + q_pat_penalty(0);
  for (int y = 0; y < q_n - 1; y++) {
    for (int x = 0; x < q_n - 1; x++) {
      uint8_t c = q_cells[y * QR_MAX_DIM + x];
      if (c == q_cells[y * QR_MAX_DIM + x + 1] && c == q_cells[(y + 1) * QR_MAX_DIM + x] &&
          c == q_cells[(y + 1) * QR_MAX_DIM + x + 1])
        res += 3;
    }
  }
  int dark = 0;
  for (int y = 0; y < q_n; y++) {
    for (int x = 0; x < q_n; x++)
      dark += q_cells[y * QR_MAX_DIM + x];
  }
  int total = q_n * q_n;
  int k = dark * 20 - total * 10;
  if (k < 0)
    k = -k;
  res += (k / total) * 10;
  return res;
}

static void q_put(int v, int n) {
  for (int i = n - 1; i >= 0; i--) {
    if ((v >> i) & 1)
      q_wp[q_wpos >> 3] |= (uint8_t)(0x80 >> (q_wpos & 7));
    q_wpos++;
  }
}

bool qr_encode(const uint8_t *data, unsigned int len, uint8_t *cells, int *size_out) {
  if (!data || !cells || !size_out)
    return false;
  q_gf_init();
  int ver = 0;
  for (int v = 1; v <= QR_MAX_VERSION; v++) {
    int cc = v < 10 ? 8 : 16;
    if (4 + cc + (int)len * 8 <= (int)qr_data_cw[v] * 8) {
      ver = v;
      break;
    }
  }
  if (!ver)
    return false;
  q_n = 4 * ver + 17;
  for (unsigned int i = 0; i < sizeof(q_data); i++)
    q_data[i] = 0;
  q_wp = q_data;
  q_wpos = 0;
  q_put(4, 4);
  q_put((int)len, ver < 10 ? 8 : 16);
  for (unsigned int i = 0; i < len; i++)
    q_put(data[i], 8);
  int cap = (int)qr_data_cw[ver] * 8;
  int term = cap - q_wpos;
  if (term > 4)
    term = 4;
  if (term > 0)
    q_put(0, term);
  q_put(0, (8 - q_wpos % 8) % 8);
  uint8_t pad = 0xEC;
  while (q_wpos < cap) {
    q_put(pad, 8);
    pad ^= 0xEC ^ 0x11;
  }
  int n1 = qr_g1_n[ver];
  int d1 = qr_g1_d[ver];
  int n2 = qr_g2_n[ver];
  int d2 = qr_g2_d[ver];
  int ec = qr_ec_cw[ver];
  int nb = n1 + n2;
  q_rs_gen(ec);
  int mp = 0;
  int maxd = d1 > d2 ? d1 : d2;
  for (int i = 0; i < maxd; i++) {
    for (int b = 0; b < nb; b++) {
      int bl = b < n1 ? d1 : d2;
      int bo = b < n1 ? b * d1 : n1 * d1 + (b - n1) * d2;
      if (i < bl)
        q_msg[mp++] = q_data[bo + i];
    }
  }
  for (int b = 0; b < nb; b++) {
    int bl = b < n1 ? d1 : d2;
    int bo = b < n1 ? b * d1 : n1 * d1 + (b - n1) * d2;
    q_rs_rem(q_data + bo, bl, ec, q_ecb + b * 32);
  }
  for (int i = 0; i < ec; i++) {
    for (int b = 0; b < nb; b++)
      q_msg[mp++] = q_ecb[b * 32 + i];
  }
  for (int y = 0; y < q_n; y++) {
    for (int x = 0; x < q_n; x++) {
      q_cells[y * QR_MAX_DIM + x] = 0;
      q_func[y * QR_MAX_DIM + x] = 0;
    }
  }
  q_finder(0, 0);
  q_finder(q_n - 7, 0);
  q_finder(0, q_n - 7);
  for (int i = 8; i < q_n - 8; i++) {
    uint8_t v = (i % 2 == 0) ? 1 : 0;
    q_set_f(6, i, v);
    q_set_f(i, 6, v);
  }
  const uint8_t *ap = qr_align[ver];
  for (int i = 0; i < ap[0]; i++) {
    for (int j = 0; j < ap[0]; j++) {
      if ((i == 0 && j == 0) || (i == 0 && j == ap[0] - 1) || (i == ap[0] - 1 && j == 0))
        continue;
      q_align_pat(ap[1 + i], ap[1 + j]);
    }
  }
  q_version(ver);
  q_format(q_fmt_bits(0));
  q_place_data(q_msg, mp * 8);
  int best_m = 0;
  int best_p = 0;
  for (int m = 0; m < 8; m++) {
    q_apply_mask(m);
    int p = q_penalty();
    if (m == 0 || p < best_p) {
      best_m = m;
      best_p = p;
    }
    q_apply_mask(m);
  }
  q_apply_mask(best_m);
  q_format(q_fmt_bits(best_m));
  for (int y = 0; y < q_n; y++) {
    for (int x = 0; x < q_n; x++)
      cells[y * QR_MAX_DIM + x] = q_cells[y * QR_MAX_DIM + x];
  }
  *size_out = q_n;
  return true;
}
