#include "kernel/diagnostics/qr.h"

#include <stdint.h>
#include <stdio.h>

#define DUMP_SCALE 4
#define DUMP_QUIET 4

int main(void) {
  static uint8_t in[1280];
  static uint8_t cells[QR_MAX_DIM * QR_MAX_DIM];
  size_t n = fread(in, 1, sizeof(in), stdin);
  if (n == 0)
    return 1;
  int size = 0;
  if (!qr_encode(in, (unsigned int)n, cells, &size))
    return 2;
  int px = (size + DUMP_QUIET * 2) * DUMP_SCALE;
  printf("P1\n%d %d\n", px, px);
  for (int y = 0; y < px; y++) {
    for (int x = 0; x < px; x++) {
      int mx = x / DUMP_SCALE - DUMP_QUIET;
      int my = y / DUMP_SCALE - DUMP_QUIET;
      int v = 0;
      if (mx >= 0 && my >= 0 && mx < size && my < size)
        v = cells[my * QR_MAX_DIM + mx];
      printf("%d ", v ? 1 : 0);
    }
    printf("\n");
  }
  return 0;
}
