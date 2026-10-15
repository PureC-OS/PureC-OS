#include "kmod_so.h"
#include "../../lib/string.h"
#include "../../mm/pmm.h"
#include "../../mm/vmm.h"
#include "../diagnostics/klog.h"
#include "../process/elf.h"

#define KSYM_MAGIC 0x505552454B53594DULL
#define KSYM_NAME_LEN 56
#define KSYM_CAP 1536
#define KMOD_MAX 16
#define KMOD_ALIGN 0x200000ULL
#define KMOD_REGION_LIMIT 0x4000000ULL
#define KMOD_UNWIND_CAP 511
#define KMOD_MAX_LOADS 8

#define KMOD_CLASS_64 2
#define KMOD_DATA_LE 1
#define KMOD_MACHINE_X64 62
#define KMOD_LOAD 1
#define KMOD_DYNAMIC 2
#define KMOD_FLAG_W 2
#define KMOD_FLAG_X 1
#define KMOD_DT_NEEDED 1
#define KMOD_SYM_SIZE 24
#define KMOD_SHN_ABS 0xFFF1
#define KMOD_RELA_WIDTH 24

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

struct kmod_ehdr {
  uint8_t ident[16];
  uint16_t type;
  uint16_t machine;
  uint32_t version;
  uint64_t entry;
  uint64_t phoff;
  uint64_t shoff;
  uint32_t flags;
  uint16_t ehsize;
  uint16_t phentsize;
  uint16_t phnum;
  uint16_t shentsize;
  uint16_t shnum;
  uint16_t shstrndx;
} __attribute__((packed));

struct kmod_phdr {
  uint32_t type;
  uint32_t flags;
  uint64_t offset;
  uint64_t vaddr;
  uint64_t paddr;
  uint64_t filesz;
  uint64_t memsz;
  uint64_t align;
} __attribute__((packed));

struct kmod_dyn {
  int64_t tag;
  uint64_t value;
} __attribute__((packed));

struct kmod_rela {
  uint64_t offset;
  uint64_t info;
  int64_t addend;
} __attribute__((packed));

struct kmod_sym {
  uint32_t name;
  uint8_t info;
  uint8_t other;
  uint16_t shndx;
  uint64_t value;
  uint64_t size;
} __attribute__((packed));

struct kmod_load {
  uint64_t offset;
  uint64_t vaddr;
  uint64_t filesz;
  uint64_t memsz;
  uint32_t flags;
};

struct kmod_dyninfo {
  uint64_t rela;
  uint64_t relasz;
  uint64_t relaent;
  uint64_t jmprel;
  uint64_t jmpsz;
  uint64_t symtab;
  uint64_t strtab;
  uint64_t strsz;
  uint64_t syment;
  uint64_t hash;
  bool needed;
};

struct kmod_slot {
  bool used;
  char name[KMOD_NAME_CAP];
  uint64_t bias;
  struct kmod_dyninfo dyn;
};

extern uint8_t __ksyms_area[];
extern uint8_t __ksyms_end[];

static struct kmod_slot g_mods[KMOD_MAX];
static uint64_t g_next;

static const struct ksym_table *ksyms_table(void) {
  return (const struct ksym_table *)__ksyms_area;
}

bool kmod_syms_ready(void) {
  const struct ksym_table *t = ksyms_table();
  return t->magic == KSYM_MAGIC && t->count <= t->cap && t->cap <= KSYM_CAP &&
         t->count > 0;
}

bool kmod_find(const char *name, uint64_t *addr) {
  if (!name || !addr || !kmod_syms_ready())
    return false;
  const struct ksym_table *t = ksyms_table();
  uint64_t lo = 0;
  uint64_t hi = t->count;
  while (lo < hi) {
    uint64_t mid = lo + (hi - lo) / 2;
    int cmp = strcmp(name, t->entries[mid].name);
    if (cmp == 0) {
      *addr = t->entries[mid].addr;
      return true;
    }
    if (cmp < 0)
      hi = mid;
    else
      lo = mid + 1;
  }
  return false;
}

static bool kmod_read(uint64_t as, uint64_t src, void *dst, uint64_t size) {
  uint8_t *out = (uint8_t *)dst;
  while (size) {
    uint64_t phys = vmm_translate(as, src);
    if (!phys)
      return false;
    uint64_t amount = PMM_PAGE_SIZE - (src & (PMM_PAGE_SIZE - 1));
    if (amount > size)
      amount = size;
    memcpy(out, pmm_physical_to_virtual(phys), amount);
    src += amount;
    out += amount;
    size -= amount;
  }
  return true;
}

static bool kmod_write(uint64_t as, uint64_t dst, const void *src,
                       uint64_t size) {
  const uint8_t *in = (const uint8_t *)src;
  while (size) {
    uint64_t phys = vmm_translate(as, dst);
    if (!phys)
      return false;
    uint64_t amount = PMM_PAGE_SIZE - (dst & (PMM_PAGE_SIZE - 1));
    if (amount > size)
      amount = size;
    memcpy(pmm_physical_to_virtual(phys), in, amount);
    dst += amount;
    in += amount;
    size -= amount;
  }
  return true;
}

static bool kmod_zero(uint64_t as, uint64_t dst, uint64_t size) {
  while (size) {
    uint64_t phys = vmm_translate(as, dst);
    if (!phys)
      return false;
    uint64_t amount = PMM_PAGE_SIZE - (dst & (PMM_PAGE_SIZE - 1));
    if (amount > size)
      amount = size;
    memset(pmm_physical_to_virtual(phys), 0, amount);
    dst += amount;
    size -= amount;
  }
  return true;
}

static bool kmod_read_cstring(uint64_t as, uint64_t addr, char *out,
                              uint64_t capacity) {
  if (!out || capacity < 2)
    return false;
  uint64_t pos = 0;
  while (pos + 1 < capacity) {
    uint64_t page_remain = PMM_PAGE_SIZE - (addr & (PMM_PAGE_SIZE - 1));
    uint64_t take = page_remain;
    if (take > capacity - 1 - pos)
      take = capacity - 1 - pos;
    if (!kmod_read(as, addr, out + pos, take))
      return false;
    for (uint64_t i = 0; i < take; i++) {
      if (out[pos + i] == 0)
        return pos + i > 0;
    }
    pos += take;
    addr += take;
  }
  return false;
}

static bool kmod_valid_ehdr(const struct kmod_ehdr *h, uint64_t size) {
  if (size < sizeof(*h))
    return false;
  if (h->ident[0] != 0x7F || h->ident[1] != 'E' || h->ident[2] != 'L' ||
      h->ident[3] != 'F' || h->ident[4] != KMOD_CLASS_64 ||
      h->ident[5] != KMOD_DATA_LE || h->type != ELF_TYPE_SHARED ||
      h->machine != KMOD_MACHINE_X64 ||
      h->phentsize != sizeof(struct kmod_phdr))
    return false;
  uint64_t tab = (uint64_t)h->phnum * h->phentsize;
  if (h->phoff > size || tab > size - h->phoff)
    return false;
  return true;
}

static bool kmod_hash_nchain(uint64_t as, uint64_t hash, uint64_t bias,
                             uint32_t *count) {
  uint8_t raw[8];
  if (!kmod_read(as, hash + bias, raw, 8))
    return false;
  uint32_t nchain = 0;
  memcpy(&nchain, raw + 4, 4);
  if (nchain == 0 || nchain > 1U << 24)
    return false;
  *count = nchain;
  return true;
}

static bool kmod_read_sym(uint64_t as, const struct kmod_dyninfo *dyn,
                          uint64_t bias, uint32_t index, struct kmod_sym *out) {
  if (!dyn->symtab || dyn->syment != KMOD_SYM_SIZE)
    return false;
  return kmod_read(as, dyn->symtab + bias + (uint64_t)index * KMOD_SYM_SIZE,
                   out, sizeof(*out));
}

static bool kmod_self_lookup(uint64_t as, const struct kmod_dyninfo *dyn,
                             uint64_t bias, const char *name, uint64_t *value) {
  if (!dyn->symtab || !dyn->strtab || !dyn->strsz || !dyn->hash)
    return false;
  if (dyn->syment != KMOD_SYM_SIZE)
    return false;
  uint32_t nchain = 0;
  if (!kmod_hash_nchain(as, dyn->hash, bias, &nchain))
    return false;
  for (uint32_t pass = 0; pass < 2; pass++) {
    for (uint32_t i = 1; i < nchain; i++) {
      struct kmod_sym s;
      if (!kmod_read_sym(as, dyn, bias, i, &s))
        return false;
      uint32_t bind = s.info >> 4;
      if (bind != ELF_STB_GLOBAL && bind != ELF_STB_WEAK)
        continue;
      if ((pass == 0 && bind != ELF_STB_GLOBAL) ||
          (pass == 1 && bind != ELF_STB_WEAK))
        continue;
      if (s.shndx == ELF_SHN_UNDEF)
        continue;
      if ((uint64_t)s.name + 1 >= dyn->strsz)
        continue;
      char cand[ELF_NAME_CAP + 32];
      if (!kmod_read_cstring(as, dyn->strtab + bias + s.name, cand,
                             sizeof(cand)))
        continue;
      if ((uint64_t)s.name + strlen(cand) >= dyn->strsz)
        continue;
      if (strcmp(cand, name) != 0)
        continue;
      if (s.shndx == KMOD_SHN_ABS)
        *value = s.value;
      else
        *value = bias + s.value;
      return true;
    }
  }
  return false;
}

static bool kmod_parse_dynamic(const uint8_t *bytes, uint64_t size,
                               uint64_t dyn_off, uint64_t dyn_filesz,
                               struct kmod_dyninfo *dyn) {
  memset(dyn, 0, sizeof(*dyn));
  dyn->syment = KMOD_SYM_SIZE;
  dyn->relaent = KMOD_RELA_WIDTH;
  if (dyn_filesz == 0 || dyn_filesz % 16 != 0)
    return false;
  if (dyn_off > size || dyn_filesz > size - dyn_off)
    return false;
  uint64_t count = dyn_filesz / 16;
  bool null_seen = false;
  for (uint64_t i = 0; i < count; i++) {
    struct kmod_dyn e;
    memcpy(&e, bytes + dyn_off + i * 16, 16);
    if (e.tag == ELF_DT_NULL) {
      null_seen = true;
      break;
    }
    if (e.tag == KMOD_DT_NEEDED)
      dyn->needed = true;
    else if (e.tag == ELF_DT_RELA)
      dyn->rela = e.value;
    else if (e.tag == ELF_DT_RELASZ)
      dyn->relasz = e.value;
    else if (e.tag == ELF_DT_RELAENT)
      dyn->relaent = e.value;
    else if (e.tag == ELF_DT_JMPREL)
      dyn->jmprel = e.value;
    else if (e.tag == ELF_DT_PLTRELSZ)
      dyn->jmpsz = e.value;
    else if (e.tag == ELF_DT_SYMTAB)
      dyn->symtab = e.value;
    else if (e.tag == ELF_DT_STRTAB)
      dyn->strtab = e.value;
    else if (e.tag == ELF_DT_STRSZ)
      dyn->strsz = e.value;
    else if (e.tag == ELF_DT_SYMENT)
      dyn->syment = e.value;
    else if (e.tag == ELF_DT_HASH)
      dyn->hash = e.value;
  }
  return null_seen;
}

static bool kmod_apply_table(uint64_t as, uint64_t bias,
                             const struct kmod_dyninfo *dyn, uint64_t table,
                             uint64_t size, const char *modname) {
  if (size == 0)
    return true;
  if (size % KMOD_RELA_WIDTH != 0)
    return false;
  uint64_t count = size / KMOD_RELA_WIDTH;
  for (uint64_t i = 0; i < count; i++) {
    struct kmod_rela r;
    if (!kmod_read(as, table + bias + i * KMOD_RELA_WIDTH, &r, sizeof(r)))
      return false;
    uint32_t type = (uint32_t)(r.info & 0xFFFFFFFFULL);
    uint32_t sym = (uint32_t)(r.info >> 32);
    if (type == ELF_R_X86_64_NONE)
      continue;
    if (type == ELF_R_X86_64_RELATIVE) {
      if (sym != 0)
        return false;
      uint64_t value = bias + (uint64_t)r.addend;
      if (!kmod_write(as, r.offset + bias, &value, 8))
        return false;
      continue;
    }
    if (type != ELF_R_X86_64_GLOB_DAT && type != ELF_R_X86_64_JUMP_SLOT &&
        type != ELF_R_X86_64_64) {
      klogf(KLOG_ERROR, "kmod: %s: unsupported reloc type %u", modname, type);
      return false;
    }
    uint64_t resolved = 0;
    if (sym == 0) {
      if (type != ELF_R_X86_64_64)
        return false;
      resolved = bias + (uint64_t)r.addend;
    } else {
      if (!dyn->symtab || !dyn->strtab || !dyn->strsz ||
          dyn->syment != KMOD_SYM_SIZE) {
        klogf(KLOG_ERROR, "kmod: %s: named reloc without symtab", modname);
        return false;
      }
      struct kmod_sym e;
      if (!kmod_read_sym(as, dyn, bias, sym, &e))
        return false;
      if (e.name >= dyn->strsz)
        return false;
      char wanted[ELF_NAME_CAP + 32];
      if (!kmod_read_cstring(as, dyn->strtab + bias + e.name, wanted,
                             sizeof(wanted)))
        return false;
      if ((uint64_t)e.name + strlen(wanted) >= dyn->strsz)
        return false;
      uint64_t found = 0;
      if (!kmod_find(wanted, &found) &&
          !kmod_self_lookup(as, dyn, bias, wanted, &found)) {
        if ((e.info >> 4) != ELF_STB_WEAK) {
          klogf(KLOG_ERROR, "kmod: %s: unresolved symbol", modname);
          return false;
        }
        found = 0;
      }
      if (type == ELF_R_X86_64_64)
        resolved = found + (uint64_t)r.addend;
      else
        resolved = found;
    }
    if (!kmod_write(as, r.offset + bias, &resolved, 8))
      return false;
  }
  return true;
}

static uint64_t kmod_region_base(void) {
  uint64_t base = ((uint64_t)__ksyms_end + KMOD_ALIGN - 1) & ~(KMOD_ALIGN - 1);
  if (base < 0xFFFFFFFF80000000ULL)
    base = 0xFFFFFFFF80000000ULL + 0x2000000ULL;
  return base;
}

uint64_t kmod_load_so(const char *modname, const void *image, uint64_t size) {
  if (!modname || !image || !kmod_syms_ready()) {
    klog(KLOG_ERROR, "kmod: bad arguments or symbol table missing");
    return 0;
  }
  const uint8_t *bytes = (const uint8_t *)image;
  const struct kmod_ehdr *h = (const struct kmod_ehdr *)bytes;
  if (!kmod_valid_ehdr(h, size)) {
    klogf(KLOG_ERROR, "kmod: %s: not a 64-bit shared object", modname);
    return 0;
  }
  struct kmod_load loads[KMOD_MAX_LOADS];
  uint64_t nloads = 0;
  uint64_t dyn_off = 0;
  uint64_t dyn_filesz = 0;
  bool have_dyn = false;
  for (uint16_t i = 0; i < h->phnum; i++) {
    struct kmod_phdr p;
    memcpy(&p, bytes + h->phoff + (uint64_t)i * h->phentsize, sizeof(p));
    if (p.type == KMOD_DYNAMIC) {
      if (have_dyn)
        return 0;
      have_dyn = true;
      dyn_off = p.offset;
      dyn_filesz = p.filesz;
      continue;
    }
    if (p.type != KMOD_LOAD)
      continue;
    if (nloads >= KMOD_MAX_LOADS)
      return 0;
    if (p.filesz > p.memsz || p.offset > size || p.filesz > size - p.offset)
      return 0;
    loads[nloads].offset = p.offset;
    loads[nloads].vaddr = p.vaddr;
    loads[nloads].filesz = p.filesz;
    loads[nloads].memsz = p.memsz;
    loads[nloads].flags = p.flags;
    nloads++;
  }
  if (!nloads)
    return 0;
  uint64_t lo = loads[0].vaddr;
  uint64_t hi = loads[0].vaddr + loads[0].memsz;
  for (uint64_t i = 1; i < nloads; i++) {
    if (loads[i].vaddr < lo)
      lo = loads[i].vaddr;
    if (loads[i].vaddr + loads[i].memsz > hi)
      hi = loads[i].vaddr + loads[i].memsz;
  }
  if (hi < lo || (lo & (PMM_PAGE_SIZE - 1)) != 0)
    return 0;
  struct kmod_dyninfo dyn;
  if (have_dyn) {
    if (!kmod_parse_dynamic(bytes, size, dyn_off, dyn_filesz, &dyn))
      return 0;
    if (dyn.needed) {
      klogf(KLOG_ERROR, "kmod: %s: dependencies not supported", modname);
      return 0;
    }
  } else {
    memset(&dyn, 0, sizeof(dyn));
  }
  if (g_next == 0)
    g_next = kmod_region_base();
  uint64_t bias = (g_next + KMOD_ALIGN - 1) & ~(KMOD_ALIGN - 1);
  uint64_t first = (lo + bias) & ~(PMM_PAGE_SIZE - 1);
  uint64_t end = (hi + bias + PMM_PAGE_SIZE - 1) & ~(PMM_PAGE_SIZE - 1);
  if (end <= first || end - first > KMOD_REGION_LIMIT)
    return 0;
  uint64_t as = vmm_kernel_address_space();
  if (!as)
    return 0;
  uint64_t pages = (end - first) / PMM_PAGE_SIZE;
  if (pages > KMOD_UNWIND_CAP) {
    klogf(KLOG_ERROR, "kmod: %s: image too large", modname);
    return 0;
  }
  uint64_t unw_phys = pmm_allocate_page();
  if (!unw_phys)
    return 0;
  uint64_t *unw = (uint64_t *)pmm_physical_to_virtual(unw_phys);
  uint64_t nunw = 0;
  bool failed = false;
  for (uint64_t a = first; a < end; a += PMM_PAGE_SIZE) {
    uint64_t phys = pmm_allocate_page();
    if (!phys || !vmm_map_page(as, a, phys, VMM_PAGE_WRITABLE | VMM_PAGE_NX)) {
      if (phys)
        pmm_free_page(phys);
      failed = true;
      break;
    }
    unw[nunw++] = phys;
  }
  if (failed) {
    for (uint64_t i = 0; i < nunw; i++)
      pmm_free_page(unw[i]);
    pmm_free_page(unw_phys);
    return 0;
  }
  pmm_free_page(unw_phys);
  g_next = end;
  for (uint64_t i = 0; i < nloads; i++) {
    uint64_t dst = loads[i].vaddr + bias;
    if (loads[i].filesz &&
        !kmod_write(as, dst, bytes + loads[i].offset, loads[i].filesz))
      return 0;
    if (loads[i].memsz > loads[i].filesz &&
        !kmod_zero(as, dst + loads[i].filesz, loads[i].memsz - loads[i].filesz))
      return 0;
  }
  if (!kmod_apply_table(as, bias, &dyn, dyn.rela, dyn.relasz, modname) ||
      !kmod_apply_table(as, bias, &dyn, dyn.jmprel, dyn.jmpsz, modname))
    return 0;
  for (uint64_t a = first; a < end; a += PMM_PAGE_SIZE) {
    uint64_t want = VMM_PAGE_NX;
    for (uint64_t i = 0; i < nloads; i++) {
      uint64_t s = (loads[i].vaddr + bias) & ~(PMM_PAGE_SIZE - 1);
      uint64_t e =
          (loads[i].vaddr + bias + loads[i].memsz + PMM_PAGE_SIZE - 1) &
          ~(PMM_PAGE_SIZE - 1);
      if (a >= s && a < e) {
        want = 0;
        if (loads[i].flags & KMOD_FLAG_W)
          want |= VMM_PAGE_WRITABLE;
        if (!(loads[i].flags & KMOD_FLAG_X))
          want |= VMM_PAGE_NX;
        break;
      }
    }
    vmm_protect_page(as, a, want);
  }
  for (uint64_t i = 0; i < KMOD_MAX; i++) {
    if (!g_mods[i].used) {
      g_mods[i].used = true;
      strncpy(g_mods[i].name, modname, KMOD_NAME_CAP - 1);
      g_mods[i].name[KMOD_NAME_CAP - 1] = 0;
      g_mods[i].bias = bias;
      g_mods[i].dyn = dyn;
      klogf(KLOG_OK, "kmod: %s loaded at 0x%llx (%llu pages)", modname, bias,
            pages);
      return bias;
    }
  }
  return 0;
}

uint64_t kmod_get(const char *modname, const char *name) {
  if (!modname || !name)
    return 0;
  for (uint64_t i = 0; i < KMOD_MAX; i++) {
    if (!g_mods[i].used || strcmp(g_mods[i].name, modname) != 0)
      continue;
    uint64_t value = 0;
    if (kmod_self_lookup(vmm_kernel_address_space(), &g_mods[i].dyn,
                         g_mods[i].bias, name, &value))
      return value;
    return 0;
  }
  return 0;
}
