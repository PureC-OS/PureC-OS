#include "kernel/diagnostics/ksym.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define KSYM_MAGIC_T 0x505552454B53594DULL
#define KSYM_NAME_T 56

static uint8_t area[1024] __attribute__((aligned(8)));

static void w64(unsigned int idx, uint64_t v) {
  memcpy(area + idx * 8, &v, 8);
}

static void entry(unsigned int i, const char *name, uint64_t addr) {
  unsigned int base = 24 + i * 64;
  memset(area + base, 0, 64);
  memcpy(area + base, name, strlen(name));
  memcpy(area + base + KSYM_NAME_T, &addr, 8);
}

uint8_t __ksyms_area[1024] __attribute__((aligned(8)));

static void build_table(void) {
  memset(__ksyms_area, 0, sizeof(__ksyms_area));
  w64(0, KSYM_MAGIC_T);
  w64(1, 3);
  w64(2, 8);
  entry(0, "isr_handler", 0xFFFFFFFF80002000ULL);
  entry(1, "kernel_panic", 0xFFFFFFFF80001000ULL);
  entry(2, "syscall_handler", 0xFFFFFFFF80002149ULL);
  memcpy(__ksyms_area, area, sizeof(area));
}

static void check_ready(void) {
  build_table();
  assert(ksym_ready());
}

static void check_exact(void) {
  const char *name = 0;
  uint64_t off = 0;
  assert(ksym_resolve(0xFFFFFFFF80002000ULL, &name, &off));
  assert(strcmp(name, "isr_handler") == 0);
  assert(off == 0);
}

static void check_nearest(void) {
  const char *name = 0;
  uint64_t off = 0;
  assert(ksym_resolve(0xFFFFFFFF80002150ULL, &name, &off));
  assert(strcmp(name, "syscall_handler") == 0);
  assert(off == 7);
  assert(ksym_resolve(0xFFFFFFFF80001500ULL, &name, &off));
  assert(strcmp(name, "kernel_panic") == 0);
  assert(off == 0x500);
}

static void check_miss(void) {
  assert(!ksym_resolve(0xFFFFFFFF80000500ULL, 0, 0));
  assert(!ksym_resolve(0x1000ULL, 0, 0));
  assert(!ksym_resolve(0ULL, 0, 0));
}

static void check_broken(void) {
  uint64_t saved0, saved1;
  memcpy(&saved0, __ksyms_area, 8);
  memcpy(&saved1, __ksyms_area + 8, 8);
  memset(__ksyms_area, 0, 8);
  assert(!ksym_ready());
  assert(!ksym_resolve(0xFFFFFFFF80002000ULL, 0, 0));
  memcpy(__ksyms_area, &saved0, 8);
  memset(__ksyms_area + 8, 0, 8);
  assert(!ksym_ready());
  memcpy(__ksyms_area + 8, &saved1, 8);
  {
    uint64_t nine = 9;
    memcpy(__ksyms_area + 8, &nine, 8);
    assert(!ksym_ready());
    memcpy(__ksyms_area + 8, &saved1, 8);
  }
  assert(ksym_ready());
}

int main(void) {
  check_ready();
  check_exact();
  check_nearest();
  check_miss();
  check_broken();
  printf("ksym_resolve_test: PASS\n");
  return 0;
}
