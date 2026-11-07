#include <stddef.h>
#include <stdint.h>

#define PAGE 4096ULL
#define SYS_WRITE_N 1
#define SYS_EXIT_N 60
#define SYS_FILE_OPEN_N 200
#define SYS_FILE_READ_N 201
#define SYS_FILE_CLOSE_N 222
#define SYS_FILE_SEEK_N 275
#define SYS_MMAP_N 357
#define SYS_MUNMAP_N 358
#define SYS_MPROTECT_N 359

#define KPROT_READ 1
#define KPROT_WRITE 2
#define KPROT_EXEC 4
#define KMAP_FIXED_F 1

#define PT_NULL 0
#define PT_LOAD 1
#define PT_DYNAMIC 2
#define PT_TLS 7
#define PT_GNU_RELRO 0x6474e552

#define DT_NULL 0
#define DT_NEEDED 1
#define DT_PLTRELSZ 2
#define DT_PLTGOT 3
#define DT_HASH 4
#define DT_STRTAB 5
#define DT_SYMTAB 6
#define DT_RELA 7
#define DT_RELASZ 8
#define DT_RELAENT 9
#define DT_STRSZ 10
#define DT_SYMENT 11
#define DT_INIT 12
#define DT_FINI 13
#define DT_SONAME 14
#define DT_RPATH 15
#define DT_SYMBOLIC 16
#define DT_REL 17
#define DT_TEXTREL 22
#define DT_JMPREL 23
#define DT_BIND_NOW 24
#define DT_INIT_ARRAY 25
#define DT_FINI_ARRAY 26
#define DT_INIT_ARRAYSZ 27
#define DT_FINI_ARRAYSZ 28
#define DT_RUNPATH 29
#define DT_FLAGS 30
#define DT_GNU_HASH 0x6ffffef5
#define DT_FLAGS_1 0x6ffffffb

#define DF_BIND_NOW 8
#define DF_1_NOW 1

#define R_NONE 0
#define R_64 1
#define R_COPY 5
#define R_GLOB_DAT 6
#define R_JUMP_SLOT 7
#define R_RELATIVE 8
#define R_DTPMOD64 16
#define R_DTPOFF64 17
#define R_TPOFF64 18
#define R_TLSDESC 36
#define R_IRELATIVE 37

#define STB_LOCAL 0
#define STB_GLOBAL 1
#define STB_WEAK 2
#define STT_NOTYPE 0
#define STT_OBJECT 1
#define STT_FUNC 2
#define STT_TLS 6
#define STT_GNU_IFUNC 10
#define SHN_UNDEF 0

#define RTLD_LAZY_F 1
#define RTLD_NOW_F 2
#define RTLD_NOLOAD_F 4
#define RTLD_GLOBAL_F 256
#define RTLD_NODELETE_F 4096

#define RTLD_NEXT_H ((void *)-1)

#define EXPORT __attribute__((visibility("default")))
#define HIDDEN __attribute__((visibility("hidden")))

typedef struct { uint8_t ident[16]; uint16_t type, machine; uint32_t version; uint64_t entry, phoff, shoff; uint32_t flags; uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx; } Ehdr;
typedef struct { uint32_t type, flags; uint64_t offset, vaddr, paddr, filesz, memsz, align; } Phdr;
typedef struct { int64_t tag; uint64_t val; } Dyn;
typedef struct { uint32_t name; uint8_t info, other; uint16_t shndx; uint64_t value, size; } Sym;
typedef struct { uint64_t offset, info; int64_t addend; } Rela;

struct obj {
  struct obj *next;
  struct obj *gnext;
  struct obj *cnext;
  uint64_t base;
  uint64_t lo;
  uint64_t hi;
  const char *path;
  const char *soname;
  const Phdr *phdr;
  uint16_t phnum;
  const Dyn *dyn;
  const Sym *symtab;
  const char *strtab;
  const uint32_t *hash;
  const uint32_t *gnu_hash;
  const Rela *rela;
  uint64_t relasz;
  const Rela *jmprel;
  uint64_t pltrelsz;
  uint64_t *pltgot;
  uint64_t init;
  uint64_t fini;
  const uint64_t *init_array;
  uint64_t init_arraysz;
  const uint64_t *fini_array;
  uint64_t fini_arraysz;
  const char *runpath;
  struct obj **deps;
  uint64_t ndeps;
  struct obj **scope;
  uint64_t nscope;
  uint64_t refs;
  uint8_t is_exe;
  uint8_t relocated;
  uint8_t inited;
  uint8_t finied;
  uint8_t global;
  uint8_t bind_now;
  uint8_t pinned;
  uint8_t symbolic;
};

struct dl_info_t {
  const char *dli_fname;
  void *dli_fbase;
  const char *dli_sname;
  void *dli_saddr;
};

struct dl_phdr_info_t {
  uint64_t dlpi_addr;
  const char *dlpi_name;
  const Phdr *dlpi_phdr;
  uint16_t dlpi_phnum;
  uint64_t dlpi_adds;
  uint64_t dlpi_subs;
  uint64_t dlpi_tls_modid;
  void *dlpi_tls_data;
};

extern const Ehdr __ehdr_start HIDDEN;
extern const Dyn _DYNAMIC[] HIDDEN;
extern void _start(void) HIDDEN;
extern void _pc_dl_resolve(void) HIDDEN;

static struct obj *objs_head;
static struct obj *objs_tail;
static struct obj *global_head;
static struct obj *global_tail;
static struct obj *main_obj;
static const char *const *env_vector;
static const char *library_path_env;
static int force_now;
static uint64_t adds_counter;
static uint64_t subs_counter;
static char error_text[256];
static int error_set;
static uint8_t *arena_cur;
static uint64_t arena_left;

static int64_t sys3(uint64_t number, uint64_t a, uint64_t b, uint64_t c)
{
  int64_t result;
  __asm__ volatile("int $0x80"
                   : "=a"(result)
                   : "a"(number), "b"(a), "c"(b), "d"(c)
                   : "r10", "r8", "memory");
  return result;
}

struct mmap_req { uint64_t address, length, prot, flags; };

static int64_t sys_mmap(uint64_t address, uint64_t length, uint64_t prot, uint64_t flags)
{
  struct mmap_req req = {address, length, prot, flags};
  return sys3(SYS_MMAP_N, (uint64_t)&req, 0, 0);
}

HIDDEN void *memcpy(void *dst, const void *src, size_t n)
{
  uint8_t *d = dst;
  const uint8_t *s = src;
  while (n--)
    *d++ = *s++;
  return dst;
}

HIDDEN void *memset(void *dst, int c, size_t n)
{
  uint8_t *d = dst;
  while (n--)
    *d++ = (uint8_t)c;
  return dst;
}

HIDDEN void *memmove(void *dst, const void *src, size_t n)
{
  uint8_t *d = dst;
  const uint8_t *s = src;
  if (d < s)
  {
    while (n--)
      *d++ = *s++;
  }
  else
  {
    d += n;
    s += n;
    while (n--)
      *--d = *--s;
  }
  return dst;
}

static uint64_t slen(const char *s)
{
  uint64_t n = 0;
  while (s[n])
    n++;
  return n;
}

static int scmp(const char *a, const char *b)
{
  while (*a && *a == *b)
  {
    a++;
    b++;
  }
  return (uint8_t)*a - (uint8_t)*b;
}

static void write_err(const char *s)
{
  sys3(SYS_WRITE_N, (uint64_t)s, slen(s), 2);
}

static void fatal(const char *a, const char *b)
{
  write_err("pc-ld: ");
  write_err(a);
  if (b)
  {
    write_err(": ");
    write_err(b);
  }
  write_err("\n");
  sys3(SYS_EXIT_N, 127, 0, 0);
  for (;;)
    __asm__ volatile("hlt");
}

static void set_error(const char *a, const char *b)
{
  uint64_t n = 0;
  const char *parts[2] = {a, b};
  for (int i = 0; i < 2; i++)
  {
    if (!parts[i])
      continue;
    if (i == 1 && n + 2 < sizeof(error_text))
    {
      error_text[n++] = ':';
      error_text[n++] = ' ';
    }
    for (const char *c = parts[i]; *c && n + 1 < sizeof(error_text); c++)
      error_text[n++] = *c;
  }
  error_text[n] = 0;
  error_set = 1;
}

static void *ld_alloc(uint64_t size)
{
  size = (size + 15) & ~15ULL;
  if (size > arena_left)
  {
    uint64_t chunk = (size + 0xFFFFULL) & ~0xFFFFULL;
    if (chunk < 0x10000ULL)
      chunk = 0x10000ULL;
    int64_t base = sys_mmap(0, chunk, KPROT_READ | KPROT_WRITE, 0);
    if (base < 0)
      return 0;
    arena_cur = (uint8_t *)base;
    arena_left = chunk;
  }
  void *out = arena_cur;
  arena_cur += size;
  arena_left -= size;
  return out;
}

static char *dup_str(const char *s)
{
  uint64_t n = slen(s) + 1;
  char *p = ld_alloc(n);
  if (p)
    memcpy(p, s, n);
  return p;
}

static uint32_t sysv_hash(const char *name)
{
  uint32_t h = 0;
  for (const uint8_t *p = (const uint8_t *)name; *p; p++)
  {
    h = (h << 4) + *p;
    uint32_t g = h & 0xf0000000u;
    if (g)
      h ^= g >> 24;
    h &= ~g;
  }
  return h;
}

static uint32_t gnu_hash_fn(const char *name)
{
  uint32_t h = 5381;
  for (const uint8_t *p = (const uint8_t *)name; *p; p++)
    h = h * 33 + *p;
  return h;
}

static int sym_usable(const Sym *s)
{
  if (s->shndx == SHN_UNDEF)
    return 0;
  uint8_t type = s->info & 15;
  uint8_t bind = s->info >> 4;
  if (type == STT_TLS)
    return 0;
  return bind == STB_GLOBAL || bind == STB_WEAK;
}

static const Sym *lookup_in_obj(const struct obj *o, const char *name,
                                uint32_t sh, uint32_t gh)
{
  if (o->gnu_hash)
  {
    const uint32_t *h = o->gnu_hash;
    uint32_t nbuckets = h[0];
    uint32_t symoffset = h[1];
    uint32_t bloom_size = h[2];
    uint32_t bloom_shift = h[3];
    const uint64_t *bloom = (const uint64_t *)(h + 4);
    const uint32_t *buckets = (const uint32_t *)(bloom + bloom_size);
    const uint32_t *chain = buckets + nbuckets;
    uint64_t word = bloom[(gh / 64) % bloom_size];
    uint64_t mask = (1ULL << (gh % 64)) | (1ULL << ((gh >> bloom_shift) % 64));
    if ((word & mask) != mask)
      return 0;
    uint32_t idx = buckets[gh % nbuckets];
    if (idx < symoffset)
      return 0;
    for (;; idx++)
    {
      uint32_t ch = chain[idx - symoffset];
      if ((ch | 1) == (gh | 1))
      {
        const Sym *s = &o->symtab[idx];
        if (sym_usable(s) && scmp(o->strtab + s->name, name) == 0)
          return s;
      }
      if (ch & 1)
        break;
    }
    return 0;
  }
  if (o->hash)
  {
    uint32_t nbucket = o->hash[0];
    const uint32_t *buckets = o->hash + 2;
    const uint32_t *chains = buckets + nbucket;
    if (!nbucket)
      return 0;
    for (uint32_t idx = buckets[sh % nbucket]; idx; idx = chains[idx])
    {
      const Sym *s = &o->symtab[idx];
      if (sym_usable(s) && scmp(o->strtab + s->name, name) == 0)
        return s;
    }
  }
  return 0;
}

struct found {
  const Sym *sym;
  const struct obj *obj;
};

static struct found lookup_global(const char *name, uint32_t sh, uint32_t gh,
                                  const struct obj *skip)
{
  struct found f = {0, 0};
  for (const struct obj *o = global_head; o; o = o->gnext)
  {
    if (o == skip)
      continue;
    const Sym *s = lookup_in_obj(o, name, sh, gh);
    if (s)
    {
      f.sym = s;
      f.obj = o;
      return f;
    }
  }
  return f;
}

static struct found lookup_scope(struct obj *const *scope, uint64_t n,
                                 const char *name, uint32_t sh, uint32_t gh,
                                 const struct obj *skip)
{
  struct found f = {0, 0};
  for (uint64_t i = 0; i < n; i++)
  {
    if (scope[i] == skip)
      continue;
    const Sym *s = lookup_in_obj(scope[i], name, sh, gh);
    if (s)
    {
      f.sym = s;
      f.obj = scope[i];
      return f;
    }
  }
  return f;
}

static struct found find_symbol(const struct obj *ref, const char *name,
                                const struct obj *skip)
{
  uint32_t sh = sysv_hash(name);
  uint32_t gh = gnu_hash_fn(name);
  struct found f = {0, 0};
  if (ref->symbolic)
    f = lookup_scope(ref->scope, ref->nscope, name, sh, gh, skip);
  if (!f.sym)
    f = lookup_global(name, sh, gh, skip);
  if (!f.sym && !ref->symbolic)
    f = lookup_scope(ref->scope, ref->nscope, name, sh, gh, skip);
  return f;
}

static uint64_t sym_address(const struct found *f)
{
  uint64_t value = f->obj->base + f->sym->value;
  if ((f->sym->info & 15) == STT_GNU_IFUNC)
    value = ((uint64_t(*)(void))value)();
  return value;
}

static int ld_open(const char *path)
{
  return (int)sys3(SYS_FILE_OPEN_N, (uint64_t)path, 0, 0);
}

static void ld_close(int fd)
{
  sys3(SYS_FILE_CLOSE_N, (uint64_t)fd, 0, 0);
}

static int read_at(int fd, uint64_t offset, void *buffer, uint64_t length)
{
  if (sys3(SYS_FILE_SEEK_N, (uint64_t)fd, offset, 0) < 0)
    return -1;
  uint8_t *out = buffer;
  while (length)
  {
    uint64_t chunk = length > 0x40000ULL ? 0x40000ULL : length;
    int64_t got = sys3(SYS_FILE_READ_N, (uint64_t)fd, (uint64_t)out, chunk);
    if (got <= 0)
      return -1;
    out += got;
    length -= (uint64_t)got;
  }
  return 0;
}

static uint32_t seg_prot(uint32_t flags)
{
  uint32_t p = 0;
  if (flags & 4)
    p |= KPROT_READ;
  if (flags & 2)
    p |= KPROT_WRITE;
  if (flags & 1)
    p |= KPROT_EXEC;
  return p;
}

static void parse_dynamic(struct obj *o)
{
  const Dyn *d = o->dyn;
  uint64_t strtab = 0, symtab = 0, hash = 0, gnu_hash = 0, rela = 0, jmprel = 0;
  uint64_t pltgot = 0, init_array = 0, fini_array = 0;
  uint64_t runpath = (uint64_t)-1, rpath = (uint64_t)-1, soname = (uint64_t)-1;
  for (; d->tag != DT_NULL; d++)
  {
    switch (d->tag)
    {
    case DT_STRTAB: strtab = d->val; break;
    case DT_SYMTAB: symtab = d->val; break;
    case DT_HASH: hash = d->val; break;
    case DT_GNU_HASH: gnu_hash = d->val; break;
    case DT_RELA: rela = d->val; break;
    case DT_RELASZ: o->relasz = d->val; break;
    case DT_JMPREL: jmprel = d->val; break;
    case DT_PLTRELSZ: o->pltrelsz = d->val; break;
    case DT_PLTGOT: pltgot = d->val; break;
    case DT_INIT: o->init = d->val; break;
    case DT_FINI: o->fini = d->val; break;
    case DT_INIT_ARRAY: init_array = d->val; break;
    case DT_INIT_ARRAYSZ: o->init_arraysz = d->val; break;
    case DT_FINI_ARRAY: fini_array = d->val; break;
    case DT_FINI_ARRAYSZ: o->fini_arraysz = d->val; break;
    case DT_RUNPATH: runpath = d->val; break;
    case DT_RPATH: rpath = d->val; break;
    case DT_SONAME: soname = d->val; break;
    case DT_SYMBOLIC: o->symbolic = 1; break;
    case DT_BIND_NOW: o->bind_now = 1; break;
    case DT_FLAGS:
      if (d->val & DF_BIND_NOW)
        o->bind_now = 1;
      break;
    case DT_FLAGS_1:
      if (d->val & DF_1_NOW)
        o->bind_now = 1;
      break;
    default: break;
    }
  }
  if (strtab)
    o->strtab = (const char *)(o->base + strtab);
  if (symtab)
    o->symtab = (const Sym *)(o->base + symtab);
  if (hash)
    o->hash = (const uint32_t *)(o->base + hash);
  if (gnu_hash)
    o->gnu_hash = (const uint32_t *)(o->base + gnu_hash);
  if (rela)
    o->rela = (const Rela *)(o->base + rela);
  if (jmprel)
    o->jmprel = (const Rela *)(o->base + jmprel);
  if (pltgot)
    o->pltgot = (uint64_t *)(o->base + pltgot);
  if (init_array)
    o->init_array = (const uint64_t *)(o->base + init_array);
  if (fini_array)
    o->fini_array = (const uint64_t *)(o->base + fini_array);
  if (o->strtab)
  {
    if (runpath != (uint64_t)-1)
      o->runpath = o->strtab + runpath;
    else if (rpath != (uint64_t)-1)
      o->runpath = o->strtab + rpath;
    if (soname != (uint64_t)-1)
      o->soname = o->strtab + soname;
  }
}

static void link_obj(struct obj *o)
{
  o->next = 0;
  if (objs_tail)
    objs_tail->next = o;
  else
    objs_head = o;
  objs_tail = o;
}

static void global_add(struct obj *o)
{
  if (o->global)
    return;
  o->global = 1;
  o->gnext = 0;
  if (global_tail)
    global_tail->gnext = o;
  else
    global_head = o;
  global_tail = o;
}

static void global_remove(struct obj *o)
{
  if (!o->global)
    return;
  o->global = 0;
  struct obj *prev = 0;
  for (struct obj *c = global_head; c; prev = c, c = c->gnext)
  {
    if (c != o)
      continue;
    if (prev)
      prev->gnext = c->gnext;
    else
      global_head = c->gnext;
    if (global_tail == c)
      global_tail = prev;
    break;
  }
  o->gnext = 0;
}

static void unlink_obj(struct obj *o)
{
  struct obj *prev = 0;
  for (struct obj *c = objs_head; c; prev = c, c = c->next)
  {
    if (c != o)
      continue;
    if (prev)
      prev->next = c->next;
    else
      objs_head = c->next;
    if (objs_tail == c)
      objs_tail = prev;
    break;
  }
}

static int map_object(struct obj *o, int fd, const Ehdr *eh, const Phdr *ph)
{
  uint64_t lo = ~0ULL, hi = 0;
  for (uint16_t i = 0; i < eh->phnum; i++)
  {
    if (ph[i].type == PT_TLS && ph[i].memsz)
    {
      set_error(o->path, "thread-local storage is not supported");
      return -1;
    }
    if (ph[i].type != PT_LOAD)
      continue;
    uint64_t s = ph[i].vaddr & ~(PAGE - 1);
    uint64_t e = (ph[i].vaddr + ph[i].memsz + PAGE - 1) & ~(PAGE - 1);
    if (s < lo)
      lo = s;
    if (e > hi)
      hi = e;
  }
  if (lo == ~0ULL || hi <= lo)
  {
    set_error(o->path, "no loadable segments");
    return -1;
  }
  int64_t base = sys_mmap(0, hi - lo, 0, 0);
  if (base < 0)
  {
    set_error(o->path, "cannot reserve address space");
    return -1;
  }
  o->base = (uint64_t)base - lo;
  o->lo = (uint64_t)base;
  o->hi = (uint64_t)base + (hi - lo);
  uint64_t covered = 0;
  for (uint16_t i = 0; i < eh->phnum; i++)
  {
    if (ph[i].type != PT_LOAD)
      continue;
    uint64_t s = (o->base + ph[i].vaddr) & ~(PAGE - 1);
    uint64_t e = (o->base + ph[i].vaddr + ph[i].memsz + PAGE - 1) & ~(PAGE - 1);
    if (s < covered)
      s = covered;
    if (e > s)
    {
      if (sys_mmap(s, e - s, KPROT_READ | KPROT_WRITE, KMAP_FIXED_F) < 0)
      {
        set_error(o->path, "cannot map segment");
        return -1;
      }
      covered = e;
    }
    if (ph[i].filesz &&
        read_at(fd, ph[i].offset, (void *)(o->base + ph[i].vaddr), ph[i].filesz) < 0)
    {
      set_error(o->path, "cannot read segment");
      return -1;
    }
  }
  Phdr *copy = ld_alloc((uint64_t)eh->phnum * sizeof(Phdr));
  if (!copy)
    return -1;
  memcpy(copy, ph, (uint64_t)eh->phnum * sizeof(Phdr));
  o->phdr = copy;
  o->phnum = eh->phnum;
  for (uint16_t i = 0; i < eh->phnum; i++)
  {
    if (ph[i].type == PT_DYNAMIC)
      o->dyn = (const Dyn *)(o->base + ph[i].vaddr);
  }
  if (!o->dyn)
  {
    set_error(o->path, "not a dynamic object");
    return -1;
  }
  return 0;
}

static void protect_object(struct obj *o)
{
  uint64_t prev_hi = 0;
  uint32_t prev_prot = 0;
  for (uint16_t i = 0; i < o->phnum; i++)
  {
    const Phdr *p = &o->phdr[i];
    if (p->type != PT_LOAD)
      continue;
    uint64_t s = (o->base + p->vaddr) & ~(PAGE - 1);
    uint64_t e = (o->base + p->vaddr + p->memsz + PAGE - 1) & ~(PAGE - 1);
    uint32_t prot = seg_prot(p->flags);
    if (s < prev_hi)
    {
      sys3(SYS_MPROTECT_N, s, PAGE, prot | prev_prot);
      s += PAGE;
    }
    if (e > s)
      sys3(SYS_MPROTECT_N, s, e - s, prot);
    prev_hi = e;
    prev_prot = prot;
  }
  for (uint16_t i = 0; i < o->phnum; i++)
  {
    const Phdr *p = &o->phdr[i];
    if (p->type != PT_GNU_RELRO || !p->memsz)
      continue;
    uint64_t s = (o->base + p->vaddr) & ~(PAGE - 1);
    uint64_t e = (o->base + p->vaddr + p->memsz) & ~(PAGE - 1);
    if (e > s)
      sys3(SYS_MPROTECT_N, s, e - s, KPROT_READ);
  }
}

static int obj_matches(const struct obj *o, const char *name)
{
  if (scmp(o->path, name) == 0)
    return 1;
  if (o->soname && scmp(o->soname, name) == 0)
    return 1;
  const char *base = name;
  for (const char *c = name; *c; c++)
    if (*c == '/')
      base = c + 1;
  if (o->soname && scmp(o->soname, base) == 0)
    return 1;
  const char *pb = o->path;
  for (const char *c = o->path; *c; c++)
    if (*c == '/')
      pb = c + 1;
  return scmp(pb, base) == 0;
}

static struct obj *find_loaded(const char *name)
{
  for (struct obj *o = objs_head; o; o = o->next)
    if (obj_matches(o, name))
      return o;
  return 0;
}

static int try_open_in(const char *dir, uint64_t dlen, const char *name, char *out, uint64_t cap)
{
  uint64_t n = 0;
  if (dlen + 1 + slen(name) + 1 > cap)
    return -1;
  memcpy(out, dir, dlen);
  n = dlen;
  if (n && out[n - 1] != '/')
    out[n++] = '/';
  memcpy(out + n, name, slen(name) + 1);
  return ld_open(out);
}

static int open_library(const struct obj *requester, const char *name, char *path, uint64_t cap)
{
  int has_slash = 0;
  for (const char *c = name; *c; c++)
    if (*c == '/')
      has_slash = 1;
  if (has_slash)
  {
    uint64_t n = slen(name);
    if (n + 1 > cap)
      return -1;
    memcpy(path, name, n + 1);
    return ld_open(path);
  }
  const char *lists[3] = {library_path_env, requester ? requester->runpath : 0, 0};
  for (int i = 0; i < 2; i++)
  {
    const char *list = lists[i];
    while (list && *list)
    {
      const char *end = list;
      while (*end && *end != ':')
        end++;
      if (end > list)
      {
        int fd = try_open_in(list, (uint64_t)(end - list), name, path, cap);
        if (fd >= 0)
          return fd;
      }
      list = *end ? end + 1 : end;
    }
  }
  static const char *const defaults[] = {"/lib", "/usr/lib", "/lib64", 0};
  for (int i = 0; defaults[i]; i++)
  {
    int fd = try_open_in(defaults[i], slen(defaults[i]), name, path, cap);
    if (fd >= 0)
      return fd;
  }
  return -1;
}

static struct obj *load_file(int fd, const char *path)
{
  Ehdr eh;
  if (read_at(fd, 0, &eh, sizeof(eh)) < 0 || eh.ident[0] != 0x7f ||
      eh.ident[1] != 'E' || eh.ident[2] != 'L' || eh.ident[3] != 'F' ||
      eh.ident[4] != 2 || eh.type != 3 || eh.machine != 62 ||
      eh.phentsize != sizeof(Phdr) || eh.phnum == 0)
  {
    set_error(path, "not a valid shared object");
    return 0;
  }
  Phdr *ph = ld_alloc((uint64_t)eh.phnum * sizeof(Phdr));
  struct obj *o = ld_alloc(sizeof(struct obj));
  if (!ph || !o)
  {
    set_error(path, "out of memory");
    return 0;
  }
  if (read_at(fd, eh.phoff, ph, (uint64_t)eh.phnum * sizeof(Phdr)) < 0)
  {
    set_error(path, "cannot read program headers");
    return 0;
  }
  memset(o, 0, sizeof(*o));
  o->path = dup_str(path);
  if (map_object(o, fd, &eh, ph) < 0)
  {
    if (o->lo)
      sys3(SYS_MUNMAP_N, o->lo, o->hi - o->lo, 0);
    return 0;
  }
  parse_dynamic(o);
  if (!o->symtab || !o->strtab)
  {
    set_error(path, "missing symbol table");
    sys3(SYS_MUNMAP_N, o->lo, o->hi - o->lo, 0);
    return 0;
  }
  o->refs = 0;
  link_obj(o);
  adds_counter++;
  return o;
}

static int load_deps(struct obj *o, struct obj **created);

static struct obj *load_named(const struct obj *requester, const char *name,
                              struct obj **created)
{
  struct obj *have = find_loaded(name);
  if (have)
    return have;
  char path[512];
  int fd = open_library(requester, name, path, sizeof(path));
  if (fd < 0)
  {
    set_error(name, "cannot open shared object file");
    return 0;
  }
  struct obj *o = load_file(fd, path);
  ld_close(fd);
  if (!o)
    return 0;
  o->cnext = *created;
  *created = o;
  if (load_deps(o, created) < 0)
    return 0;
  return o;
}

static uint64_t count_needed(const struct obj *o)
{
  uint64_t n = 0;
  for (const Dyn *d = o->dyn; d->tag != DT_NULL; d++)
    if (d->tag == DT_NEEDED)
      n++;
  return n;
}

static int load_deps(struct obj *o, struct obj **created)
{
  uint64_t n = count_needed(o);
  o->ndeps = 0;
  if (!n)
    return 0;
  o->deps = ld_alloc(n * sizeof(struct obj *));
  if (!o->deps)
  {
    set_error(o->path, "out of memory");
    return -1;
  }
  for (const Dyn *d = o->dyn; d->tag != DT_NULL; d++)
  {
    if (d->tag != DT_NEEDED)
      continue;
    struct obj *dep = load_named(o, o->strtab + d->val, created);
    if (!dep)
      return -1;
    o->deps[o->ndeps++] = dep;
  }
  return 0;
}

static int scope_has(struct obj **list, uint64_t n, const struct obj *o)
{
  for (uint64_t i = 0; i < n; i++)
    if (list[i] == o)
      return 1;
  return 0;
}

static int build_scope(struct obj *root)
{
  uint64_t cap = 8;
  struct obj **list = ld_alloc(cap * sizeof(struct obj *));
  if (!list)
    return -1;
  uint64_t n = 0;
  list[n++] = root;
  for (uint64_t i = 0; i < n; i++)
  {
    struct obj *o = list[i];
    for (uint64_t k = 0; k < o->ndeps; k++)
    {
      if (scope_has(list, n, o->deps[k]))
        continue;
      if (n == cap)
      {
        struct obj **bigger = ld_alloc(cap * 2 * sizeof(struct obj *));
        if (!bigger)
          return -1;
        memcpy(bigger, list, n * sizeof(struct obj *));
        list = bigger;
        cap *= 2;
      }
      list[n++] = o->deps[k];
    }
  }
  root->scope = list;
  root->nscope = n;
  return 0;
}

static int reloc_one(struct obj *o, const Rela *r, int lazy)
{
  uint32_t type = (uint32_t)(r->info & 0xffffffffu);
  uint32_t symidx = (uint32_t)(r->info >> 32);
  uint64_t *where = (uint64_t *)(o->base + r->offset);
  if (type == R_NONE)
    return 0;
  if (type == R_RELATIVE)
  {
    *where = o->base + (uint64_t)r->addend;
    return 0;
  }
  if (type == R_IRELATIVE)
  {
    *where = ((uint64_t(*)(void))(o->base + (uint64_t)r->addend))();
    return 0;
  }
  if (type == R_DTPMOD64 || type == R_DTPOFF64 || type == R_TPOFF64 || type == R_TLSDESC)
  {
    set_error(o->path, "thread-local relocation is not supported");
    return -1;
  }
  if (type == R_JUMP_SLOT && lazy)
  {
    *where += o->base;
    return 0;
  }
  if (type != R_64 && type != R_GLOB_DAT && type != R_JUMP_SLOT && type != R_COPY)
  {
    set_error(o->path, "unsupported relocation type");
    return -1;
  }
  const Sym *own = &o->symtab[symidx];
  uint64_t value = 0;
  const char *name = o->strtab + own->name;
  if (symidx == 0)
  {
    value = 0;
  }
  else
  {
    struct found f;
    if (o->is_exe && type == R_COPY)
      f = find_symbol(o, name, o);
    else if ((own->info >> 4) == STB_LOCAL && own->shndx != SHN_UNDEF)
    {
      f.sym = own;
      f.obj = o;
    }
    else
      f = find_symbol(o, name, type == R_COPY ? o : 0);
    if (!f.sym)
    {
      if ((own->info >> 4) == STB_WEAK)
      {
        if (type == R_COPY)
          return 0;
        value = 0;
      }
      else
      {
        set_error(name, "undefined symbol");
        return -1;
      }
    }
    else
    {
      value = sym_address(&f);
      if (type == R_COPY)
      {
        memcpy(where, (const void *)value, own->size);
        return 0;
      }
    }
  }
  if (type == R_64)
    *where = value + (uint64_t)r->addend;
  else
    *where = value;
  return 0;
}

static int relocate_object(struct obj *o, int lazy)
{
  if (o->relocated)
    return 0;
  o->relocated = 1;
  uint64_t count = o->relasz / sizeof(Rela);
  for (uint64_t i = 0; i < count; i++)
    if (reloc_one(o, &o->rela[i], lazy) < 0)
      return -1;
  if (o->pltgot && lazy && o->jmprel)
  {
    o->pltgot[1] = (uint64_t)o;
    o->pltgot[2] = (uint64_t)_pc_dl_resolve;
  }
  count = o->pltrelsz / sizeof(Rela);
  for (uint64_t i = 0; i < count; i++)
    if (reloc_one(o, &o->jmprel[i], lazy) < 0)
      return -1;
  return 0;
}

EXPORT uint64_t pc_dl_fixup(struct obj *o, uint64_t index)
{
  const Rela *r = &o->jmprel[index];
  const Sym *own = &o->symtab[r->info >> 32];
  const char *name = o->strtab + own->name;
  struct found f = find_symbol(o, name, 0);
  uint64_t value = 0;
  if (!f.sym)
  {
    if ((own->info >> 4) != STB_WEAK)
      fatal(name, "undefined symbol");
  }
  else
    value = sym_address(&f);
  *(uint64_t *)(o->base + r->offset) = value;
  return value;
}

static void run_init(struct obj *o)
{
  if (o->inited)
    return;
  o->inited = 1;
  for (uint64_t i = 0; i < o->ndeps; i++)
    run_init(o->deps[i]);
  if (o->init)
    ((void (*)(void))(o->base + o->init))();
  uint64_t n = o->init_arraysz / sizeof(uint64_t);
  for (uint64_t i = 0; i < n; i++)
  {
    uint64_t fn = o->init_array[i];
    if (fn && fn != ~0ULL)
      ((void (*)(void))fn)();
  }
}

static void run_fini(struct obj *o)
{
  if (!o->inited || o->finied)
    return;
  o->finied = 1;
  uint64_t n = o->fini_arraysz / sizeof(uint64_t);
  for (uint64_t i = n; i > 0; i--)
  {
    uint64_t fn = o->fini_array[i - 1];
    if (fn && fn != ~0ULL)
      ((void (*)(void))fn)();
  }
  if (o->fini)
    ((void (*)(void))(o->base + o->fini))();
}

static void release_object(struct obj *o);

static void drop_ref(struct obj *o)
{
  if (o->refs == 0)
    return;
  o->refs--;
  if (o->refs == 0 && !o->pinned && !o->is_exe)
    release_object(o);
}

static void release_object(struct obj *o)
{
  run_fini(o);
  global_remove(o);
  unlink_obj(o);
  struct obj **deps = o->deps;
  uint64_t ndeps = o->ndeps;
  uint64_t lo = o->lo;
  uint64_t size = o->hi - o->lo;
  o->deps = 0;
  o->ndeps = 0;
  subs_counter++;
  sys3(SYS_MUNMAP_N, lo, size, 0);
  for (uint64_t i = 0; i < ndeps; i++)
    drop_ref(deps[i]);
}

static void discard_created(struct obj *created)
{
  for (struct obj *o = created; o;)
  {
    struct obj *next = o->cnext;
    global_remove(o);
    unlink_obj(o);
    sys3(SYS_MUNMAP_N, o->lo, o->hi - o->lo, 0);
    o = next;
  }
}

static const char *env_lookup(const char *name)
{
  if (!env_vector)
    return 0;
  uint64_t n = slen(name);
  for (const char *const *e = env_vector; *e; e++)
  {
    uint64_t i = 0;
    while (i < n && (*e)[i] == name[i])
      i++;
    if (i == n && (*e)[n] == '=')
      return *e + n + 1;
  }
  return 0;
}

static void finish_new(struct obj *created)
{
  for (struct obj *o = created; o; o = o->cnext)
    for (uint64_t i = 0; i < o->ndeps; i++)
      o->deps[i]->refs++;
}

HIDDEN uint64_t pc_dl_start(uint64_t *frame)
{
  uint64_t argc = frame[1];
  if (argc > 0 && argc < 0x10000)
  {
    env_vector = (const char *const *)(frame + 2 + argc + 1);
    library_path_env = env_lookup("LD_LIBRARY_PATH");
    if (env_lookup("LD_BIND_NOW"))
      force_now = 1;
  }
  const Ehdr *eh = &__ehdr_start;
  const Phdr *ph = (const Phdr *)((const uint8_t *)eh + eh->phoff);
  uint64_t base = 0;
  for (uint16_t i = 0; i < eh->phnum; i++)
  {
    if (ph[i].type == PT_LOAD && ph[i].offset == 0)
    {
      base = (uint64_t)eh - ph[i].vaddr;
      break;
    }
  }
  struct obj *exe = ld_alloc(sizeof(struct obj));
  if (!exe)
    fatal("out of memory", 0);
  memset(exe, 0, sizeof(*exe));
  exe->base = base;
  exe->path = "/proc/self/exe";
  exe->phdr = ph;
  exe->phnum = eh->phnum;
  exe->dyn = _DYNAMIC;
  exe->is_exe = 1;
  exe->pinned = 1;
  exe->refs = 1;
  exe->lo = base;
  exe->hi = base;
  for (uint16_t i = 0; i < eh->phnum; i++)
  {
    if (ph[i].type == PT_TLS && ph[i].memsz)
      fatal("thread-local storage is not supported", 0);
    if (ph[i].type == PT_LOAD && base + ph[i].vaddr + ph[i].memsz > exe->hi)
      exe->hi = base + ph[i].vaddr + ph[i].memsz;
  }
  parse_dynamic(exe);
  main_obj = exe;
  link_obj(exe);
  global_add(exe);
  struct obj *created = 0;
  if (load_deps(exe, &created) < 0)
    fatal(error_text, 0);
  if (build_scope(exe) < 0)
    fatal("out of memory", 0);
  for (struct obj *o = created; o; o = o->cnext)
  {
    o->pinned = 1;
    if (build_scope(o) < 0)
      fatal("out of memory", 0);
  }
  for (uint64_t i = 1; i < exe->nscope; i++)
    global_add(exe->scope[i]);
  for (uint64_t i = exe->nscope; i > 0; i--)
  {
    struct obj *o = exe->scope[i - 1];
    if (relocate_object(o, !(force_now || o->bind_now)) < 0)
      fatal(error_text, 0);
  }
  for (uint64_t i = 1; i < exe->nscope; i++)
    protect_object(exe->scope[i]);
  run_init(exe);
  return (uint64_t)_start;
}

static struct obj *handle_obj(void *handle)
{
  struct obj *o = handle;
  for (struct obj *c = objs_head; c; c = c->next)
    if (c == o)
      return o;
  return 0;
}

static struct obj *obj_for_address(uint64_t addr)
{
  for (struct obj *o = objs_head; o; o = o->next)
    if (addr >= o->lo && addr < o->hi)
      return o;
  return 0;
}

EXPORT void *__pc_dl_open(const char *name, int flags, void *caller)
{
  (void)caller;
  error_set = 0;
  if (!name)
    return main_obj;
  struct obj *have = find_loaded(name);
  if (have)
  {
    if (flags & RTLD_NOLOAD_F)
    {
      if (flags & RTLD_GLOBAL_F)
        global_add(have);
      return have;
    }
    have->refs++;
    if (flags & RTLD_GLOBAL_F)
      global_add(have);
    if (flags & RTLD_NODELETE_F)
      have->pinned = 1;
    return have;
  }
  if (flags & RTLD_NOLOAD_F)
  {
    set_error(name, "shared object not loaded");
    return 0;
  }
  struct obj *created = 0;
  struct obj *root = load_named(caller ? obj_for_address((uint64_t)caller) : 0, name, &created);
  if (!root)
  {
    discard_created(created);
    return 0;
  }
  int ok = 1;
  for (struct obj *o = created; o && ok; o = o->cnext)
    if (build_scope(o) < 0)
    {
      set_error(name, "out of memory");
      ok = 0;
    }
  int lazy = !(flags & RTLD_NOW_F) && !force_now;
  for (struct obj *o = created; o && ok; o = o->cnext)
    if (relocate_object(o, lazy && !o->bind_now) < 0)
      ok = 0;
  if (!ok)
  {
    discard_created(created);
    return 0;
  }
  for (struct obj *o = created; o; o = o->cnext)
    protect_object(o);
  finish_new(created);
  root->refs++;
  if (flags & RTLD_GLOBAL_F)
    for (struct obj *o = created; o; o = o->cnext)
      global_add(o);
  if (flags & RTLD_NODELETE_F)
    root->pinned = 1;
  run_init(root);
  return root;
}

EXPORT void *__pc_dl_sym(void *handle, const char *name, void *caller)
{
  error_set = 0;
  uint32_t sh = sysv_hash(name);
  uint32_t gh = gnu_hash_fn(name);
  struct found f = {0, 0};
  if (handle == 0)
    f = lookup_global(name, sh, gh, 0);
  else if (handle == RTLD_NEXT_H)
  {
    struct obj *from = caller ? obj_for_address((uint64_t)caller) : 0;
    int seen = from == 0;
    for (struct obj *o = objs_head; o && !f.sym; o = o->next)
    {
      if (!seen)
      {
        seen = o == from;
        continue;
      }
      const Sym *s = lookup_in_obj(o, name, sh, gh);
      if (s)
      {
        f.sym = s;
        f.obj = o;
      }
    }
  }
  else
  {
    struct obj *o = handle_obj(handle);
    if (!o)
    {
      set_error("invalid handle", 0);
      return 0;
    }
    f = lookup_scope(o->scope, o->nscope, name, sh, gh, 0);
  }
  if (!f.sym)
  {
    set_error(name, "undefined symbol");
    return 0;
  }
  return (void *)sym_address(&f);
}

EXPORT int __pc_dl_close(void *handle)
{
  error_set = 0;
  struct obj *o = handle_obj(handle);
  if (!o)
  {
    set_error("invalid handle", 0);
    return -1;
  }
  if (o->is_exe)
    return 0;
  drop_ref(o);
  return 0;
}

EXPORT const char *__pc_dl_error(void)
{
  if (!error_set)
    return 0;
  error_set = 0;
  return error_text;
}

EXPORT int __pc_dl_addr(const void *addr, struct dl_info_t *info)
{
  struct obj *o = obj_for_address((uint64_t)addr);
  if (!o)
    return 0;
  info->dli_fname = o->path;
  info->dli_fbase = (void *)o->base;
  info->dli_sname = 0;
  info->dli_saddr = 0;
  uint64_t target = (uint64_t)addr;
  uint64_t best = 0;
  uint64_t count = 0;
  if (o->gnu_hash)
  {
    const uint32_t *h = o->gnu_hash;
    uint32_t nbuckets = h[0];
    uint32_t symoffset = h[1];
    uint32_t bloom_size = h[2];
    const uint64_t *bloom = (const uint64_t *)(h + 4);
    const uint32_t *buckets = (const uint32_t *)(bloom + bloom_size);
    const uint32_t *chain = buckets + nbuckets;
    uint32_t max = 0;
    for (uint32_t i = 0; i < nbuckets; i++)
      if (buckets[i] > max)
        max = buckets[i];
    if (max >= symoffset)
    {
      while (!(chain[max - symoffset] & 1))
        max++;
      count = (uint64_t)max + 1;
    }
    else
      count = symoffset;
  }
  else if (o->hash)
    count = o->hash[1];
  for (uint64_t i = 1; i < count; i++)
  {
    const Sym *s = &o->symtab[i];
    uint8_t type = s->info & 15;
    if (s->shndx == SHN_UNDEF || (type != STT_FUNC && type != STT_OBJECT && type != STT_NOTYPE))
      continue;
    uint64_t start = o->base + s->value;
    if (start <= target && start >= best && (s->size == 0 || target < start + s->size))
    {
      best = start;
      info->dli_sname = o->strtab + s->name;
      info->dli_saddr = (void *)start;
    }
  }
  return 1;
}

EXPORT int __pc_dl_iterate_phdr(int (*callback)(struct dl_phdr_info_t *, uint64_t, void *), void *data)
{
  int result = 0;
  for (struct obj *o = objs_head; o && !result;)
  {
    struct obj *next = o->next;
    struct dl_phdr_info_t info;
    memset(&info, 0, sizeof(info));
    info.dlpi_addr = o->base;
    info.dlpi_name = o->is_exe ? "" : o->path;
    info.dlpi_phdr = o->phdr;
    info.dlpi_phnum = o->phnum;
    info.dlpi_adds = adds_counter;
    info.dlpi_subs = subs_counter;
    result = callback(&info, sizeof(info), data);
    o = next;
  }
  return result;
}

EXPORT void __pc_dl_fini(void)
{
  uint64_t n = 0;
  for (struct obj *o = objs_head; o; o = o->next)
    n++;
  for (uint64_t pass = 0; pass < n; pass++)
  {
    struct obj *pick = 0;
    for (struct obj *o = objs_head; o; o = o->next)
      if (o->inited && !o->finied)
        pick = o;
    if (!pick)
      break;
    run_fini(pick);
  }
}
