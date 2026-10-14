#include "kernel/diagnostics/qr.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint8_t cells[QR_MAX_DIM * QR_MAX_DIM];
static uint8_t cells2[QR_MAX_DIM * QR_MAX_DIM];

static uint8_t at(const uint8_t *m, int x, int y) {
  return m[y * QR_MAX_DIM + x];
}

static void check_sizes(void) {
  int size = 0;
  uint8_t one[1] = {'A'};
  assert(qr_encode(one, 1, cells, &size));
  assert(size == 21);
  uint8_t b17[17];
  memset(b17, 'x', sizeof(b17));
  assert(qr_encode(b17, 17, cells, &size));
  assert(size == 21);
  uint8_t b18[18];
  memset(b18, 'x', sizeof(b18));
  assert(qr_encode(b18, 18, cells, &size));
  assert(size == 25);
  uint8_t b300[300];
  memset(b300, 'y', sizeof(b300));
  assert(qr_encode(b300, 300, cells, &size));
  assert(size == 61);
  uint8_t b1273[1273];
  memset(b1273, 'z', sizeof(b1273));
  assert(qr_encode(b1273, 1273, cells, &size));
  assert(size == 117);
  uint8_t b1300[1300];
  memset(b1300, 'z', sizeof(b1300));
  assert(!qr_encode(b1300, 1300, cells, &size));
  assert(!qr_encode(one, 1, 0, &size));
  assert(!qr_encode(0, 1, cells, &size));
  assert(!qr_encode(one, 1, cells, 0));
}

static void check_function(void) {
  int size = 0;
  uint8_t one[1] = {'A'};
  assert(qr_encode(one, 1, cells, &size));
  assert(at(cells, 0, 0) == 1);
  assert(at(cells, 6, 6) == 1);
  assert(at(cells, 1, 1) == 0);
  assert(at(cells, 2, 2) == 1);
  assert(at(cells, 7, 0) == 0);
  assert(at(cells, 0, 7) == 0);
  assert(at(cells, 6, 8) == 1);
  assert(at(cells, 6, 9) == 0);
  assert(at(cells, 8, 6) == 1);
  assert(at(cells, 9, 6) == 0);
  assert(at(cells, 8, 13) == 1);
}

static void check_format(void) {
  int size = 0;
  uint8_t one[1] = {'A'};
  assert(qr_encode(one, 1, cells, &size));
  int pos[15][2] = {{8, 0}, {8, 1}, {8, 2}, {8, 3}, {8, 4}, {8, 5}, {8, 7},
                    {8, 8}, {7, 8}, {5, 8}, {4, 8}, {3, 8}, {2, 8}, {1, 8},
                    {0, 8}};
  int v = 0;
  for (int i = 0; i < 15; i++)
    v |= at(cells, pos[i][0], pos[i][1]) << i;
  v ^= 0x5412;
  int data5 = v >> 10;
  assert((data5 >> 3) == 1);
  int r = data5;
  for (int i = 0; i < 10; i++)
    r = (r << 1) ^ (((r >> 9) & 1) * 0x537);
  assert((r & 0x3FF) == (v & 0x3FF));
}

static void check_stable(void) {
  int s1 = 0, s2 = 0;
  uint8_t in[100];
  memset(in, 'q', sizeof(in));
  memset(cells, 0, sizeof(cells));
  memset(cells2, 0, sizeof(cells2));
  assert(qr_encode(in, sizeof(in), cells, &s1));
  assert(qr_encode(in, sizeof(in), cells2, &s2));
  assert(s1 == s2);
  assert(memcmp(cells, cells2, sizeof(cells)) == 0);
}

int main(void) {
  check_sizes();
  check_function();
  check_format();
  check_stable();
  printf("qr_encode_test: PASS\n");
  return 0;
}
