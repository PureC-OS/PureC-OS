#include "ksym.h"

#define KSYM_MAGIC 0x505552454B53594DULL
#define KSYM_NAME_LEN 56
#define KSYM_CAP 1536

struct ksym_entry {
  char name[KSYM_NAME_LEN];
  uint64_t addr;
};

struct ksym_table {
  uint64_t magic;
  uint64_t count;
  uint64_t cap;
  struct ksym_entry entries[];
};

extern uint8_t __ksyms_area[];

static const struct ksym_table *ksym_table(void) {
  return (const struct ksym_table *)__ksyms_area;
}

bool ksym_ready(void) {
  const struct ksym_table *t = ksym_table();
  if (t->magic != KSYM_MAGIC)
    return false;
  if (t->cap == 0 || t->cap > KSYM_CAP)
    return false;
  if (t->count == 0 || t->count > t->cap)
    return false;
  return true;
}

bool ksym_resolve(uint64_t addr, const char **name_out, uint64_t *offset_out) {
  if (!ksym_ready())
    return false;
  if (addr < 0xffffffff80000000ULL)
    return false;
  const struct ksym_table *t = ksym_table();
  const char *best = 0;
  uint64_t best_addr = 0;
  for (uint64_t i = 0; i < t->count; i++) {
    uint64_t sym = t->entries[i].addr;
    if (sym == 0 || sym > addr)
      continue;
    if (best == 0 || sym > best_addr) {
      best = t->entries[i].name;
      best_addr = sym;
      if (sym == addr)
        break;
    }
  }
  if (!best || !best[0])
    return false;
  if (name_out)
    *name_out = best;
  if (offset_out)
    *offset_out = addr - best_addr;
  return true;
}
