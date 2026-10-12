#include "elf.h"
#include "../../lib/string.h"
#include "../../mm/oom/oom.h"
#include "../../mm/pmm.h"
#include "../../mm/vmm.h"

#define ELF_CLASS_64 2
#define ELF_DATA_LITTLE_ENDIAN 1
#define ELF_MACHINE_X86_64 62
#define ELF_TYPE_EXECUTABLE 2
#define ELF_PROGRAM_LOAD 1
#define ELF_FLAG_WRITABLE 2
#define ELF_FLAG_EXECUTABLE 1
#define ELF_DYN_TAG_WIDTH 16ULL
#define ELF_RELA_WIDTH 24ULL
#define ELF_SO_STRIDE 0x10000000ULL

struct elf64_header
{
  uint8_t identity[16];
  uint16_t type;
  uint16_t machine;
  uint32_t version;
  uint64_t entry;
  uint64_t program_offset;
  uint64_t section_offset;
  uint32_t flags;
  uint16_t header_size;
  uint16_t program_entry_size;
  uint16_t program_count;
  uint16_t section_entry_size;
  uint16_t section_count;
  uint16_t section_names;
} __attribute__((packed));

struct elf64_program_header
{
  uint32_t type;
  uint32_t flags;
  uint64_t offset;
  uint64_t virtual_address;
  uint64_t physical_address;
  uint64_t file_size;
  uint64_t memory_size;
  uint64_t alignment;
} __attribute__((packed));

struct elf64_dyn
{
  int64_t tag;
  uint64_t value;
} __attribute__((packed));

struct elf64_rela
{
  uint64_t offset;
  uint64_t info;
  int64_t addend;
} __attribute__((packed));

struct elf64_sym
{
  uint32_t name;
  uint8_t info;
  uint8_t other;
  uint16_t shndx;
  uint64_t value;
  uint64_t size;
} __attribute__((packed));

struct elf_file_dyn
{
  uint64_t needed[ELF_MAX_NEEDED];
  uint64_t needed_count;
  uint64_t strtab_vaddr;
  uint64_t strtab_size;
};

static bool add_overflows(uint64_t left, uint64_t right)
{
  return left > UINT64_MAX - right;
}

static bool copy_to_space(uint64_t address_space, uint64_t destination,
                          const uint8_t *source, uint64_t size)
{
  while (size)
  {
    uint64_t physical = vmm_translate(address_space, destination);
    if (!physical)
      return false;
    uint64_t amount = PMM_PAGE_SIZE - (destination & (PMM_PAGE_SIZE - 1));
    if (amount > size)
      amount = size;
    memcpy(pmm_physical_to_virtual(physical), source, amount);
    destination += amount;
    source += amount;
    size -= amount;
  }
  return true;
}

static bool copy_from_space(uint64_t address_space, uint64_t source,
                            uint8_t *destination, uint64_t size)
{
  while (size)
  {
    uint64_t physical = vmm_translate(address_space, source);
    if (!physical)
      return false;
    uint64_t amount = PMM_PAGE_SIZE - (source & (PMM_PAGE_SIZE - 1));
    if (amount > size)
      amount = size;
    memcpy(destination, pmm_physical_to_virtual(physical), amount);
    source += amount;
    destination += amount;
    size -= amount;
  }
  return true;
}

static bool write_u64_to_space(uint64_t address_space, uint64_t destination,
                               uint64_t value)
{
  uint8_t bytes[8];
  memcpy(bytes, &value, 8);
  return copy_to_space(address_space, destination, bytes, 8);
}

static uint16_t peek_type(const void *image, uint64_t image_size)
{
  if (!image || image_size < sizeof(struct elf64_header))
    return 0;
  const struct elf64_header *header = (const struct elf64_header *)image;
  if (header->identity[0] != 0x7F || header->identity[1] != 'E' ||
      header->identity[2] != 'L' || header->identity[3] != 'F' ||
      header->identity[4] != ELF_CLASS_64 ||
      header->identity[5] != ELF_DATA_LITTLE_ENDIAN ||
      header->machine != ELF_MACHINE_X86_64 ||
      header->program_entry_size != sizeof(struct elf64_program_header))
    return 0;
  return header->type;
}

static bool parse_dynamic(const uint8_t *bytes, uint64_t image_size,
                          uint64_t dyn_offset, uint64_t dyn_filesz,
                          uint64_t bias, struct elf_dynamic_info *dynamic)
{
  memset(dynamic, 0, sizeof(*dynamic));
  dynamic->bias = bias;
  dynamic->syment_size = 24;
  dynamic->rela_entsize = ELF_RELA_WIDTH;
  if (dyn_filesz == 0)
    return true;
  if (add_overflows(dyn_offset, dyn_filesz) ||
      dyn_offset + dyn_filesz > image_size)
    return false;
  if (dyn_filesz % ELF_DYN_TAG_WIDTH != 0)
    return false;
  uint64_t count = dyn_filesz / ELF_DYN_TAG_WIDTH;
  bool seen_null = false;
  for (uint64_t i = 0; i < count; i++)
  {
    struct elf64_dyn entry;
    memcpy(&entry, bytes + dyn_offset + i * ELF_DYN_TAG_WIDTH,
           ELF_DYN_TAG_WIDTH);
    int64_t tag = entry.tag;
    if (tag == ELF_DT_NULL)
    {
      seen_null = true;
      break;
    }
    if (tag == ELF_DT_RELA)
      dynamic->rela_address = entry.value + bias;
    else if (tag == ELF_DT_RELASZ)
      dynamic->rela_size = entry.value;
    else if (tag == ELF_DT_RELAENT)
      dynamic->rela_entsize = entry.value;
    else if (tag == ELF_DT_JMPREL)
      dynamic->jmprel_address = entry.value + bias;
    else if (tag == ELF_DT_PLTRELSZ)
      dynamic->jmprel_size = entry.value;
    else if (tag == ELF_DT_SYMTAB)
      dynamic->symtab_address = entry.value + bias;
    else if (tag == ELF_DT_STRTAB)
      dynamic->strtab_address = entry.value + bias;
    else if (tag == ELF_DT_STRSZ)
      dynamic->strtab_size = entry.value;
    else if (tag == ELF_DT_SYMENT)
      dynamic->syment_size = entry.value;
    else if (tag == ELF_DT_RELACOUNT)
      dynamic->relacount = entry.value;
    else if (tag == ELF_DT_HASH)
      dynamic->hash_address = entry.value + bias;
  }
  if (!seen_null)
    return false;
  dynamic->has_dynamic = true;
  return true;
}

static bool apply_rela_table(uint64_t address_space, uint64_t bias,
                             uint64_t table, uint64_t size, uint64_t entsize)
{
  if (size == 0)
    return true;
  if (entsize != ELF_RELA_WIDTH)
    return false;
  if (size % ELF_RELA_WIDTH != 0)
    return false;
  uint64_t count = size / ELF_RELA_WIDTH;
  for (uint64_t i = 0; i < count; i++)
  {
    struct elf64_rela rela;
    if (!copy_from_space(address_space, table + i * ELF_RELA_WIDTH,
                         (uint8_t *)&rela, ELF_RELA_WIDTH))
      return false;
    uint32_t type = (uint32_t)(rela.info & 0xFFFFFFFFULL);
    uint32_t sym = (uint32_t)(rela.info >> 32);
    if (type == ELF_R_X86_64_NONE)
      continue;
    if (type == ELF_R_X86_64_RELATIVE)
    {
      if (sym != 0)
        return false;
      uint64_t value;
      if (rela.addend < 0 && bias < (uint64_t)(-(rela.addend + 1)) + 1)
        return false;
      value = bias + (uint64_t)rela.addend;
      if (!write_u64_to_space(address_space, rela.offset + bias, value))
        return false;
      continue;
    }
    return false;
  }
  return true;
}

bool elf_load_user_image_biased(const void *image, uint64_t image_size,
                                uint64_t address_space, uint64_t bias,
                                struct elf_load_result *result,
                                struct elf_dynamic_info *dynamic)
{
  if (!image || !result || image_size < sizeof(struct elf64_header))
    return false;
  const struct elf64_header *header = (const struct elf64_header *)image;
  if (header->identity[0] != 0x7F || header->identity[1] != 'E' ||
      header->identity[2] != 'L' || header->identity[3] != 'F' ||
      header->identity[4] != ELF_CLASS_64 ||
      header->identity[5] != ELF_DATA_LITTLE_ENDIAN ||
      (header->type != ELF_TYPE_EXECUTABLE &&
       header->type != ELF_TYPE_SHARED) ||
      header->machine != ELF_MACHINE_X86_64 ||
      header->program_entry_size != sizeof(struct elf64_program_header))
  {
    return false;
  }
  if (header->type == ELF_TYPE_EXECUTABLE && bias != 0)
    return false;
  if (header->type == ELF_TYPE_SHARED)
  {
    if ((bias & (PMM_PAGE_SIZE - 1)) != 0)
      return false;
    if (bias < ELF_USER_MIN || bias > ELF_SO_END)
      return false;
  }
  uint64_t table_size =
      (uint64_t)header->program_count * header->program_entry_size;
  if (add_overflows(header->program_offset, table_size) ||
      header->program_offset + table_size > image_size)
    return false;

  result->lowest_address = UINT64_MAX;
  result->highest_address = 0;
  const uint8_t *bytes = (const uint8_t *)image;
  uint64_t dyn_offset = 0;
  uint64_t dyn_filesz = 0;
  bool have_dyn = false;
  for (uint16_t index = 0; index < header->program_count; index++)
  {
    const struct elf64_program_header *program =
        (const struct elf64_program_header *)(bytes + header->program_offset +
                                              (uint64_t)index *
                                                  header->program_entry_size);
    if (program->type == ELF_PROGRAM_DYNAMIC)
    {
      if (have_dyn)
        return false;
      have_dyn = true;
      dyn_offset = program->offset;
      dyn_filesz = program->file_size;
      continue;
    }
    if (program->type != ELF_PROGRAM_LOAD)
      continue;
    if (program->file_size > program->memory_size ||
        add_overflows(program->offset, program->file_size) ||
        program->offset + program->file_size > image_size ||
        add_overflows(program->virtual_address, bias) ||
        add_overflows(program->virtual_address + bias, program->memory_size))
    {
      return false;
    }
    uint64_t vaddr = program->virtual_address + bias;
    if (vaddr < ELF_USER_MIN || vaddr + program->memory_size > ELF_USER_MAX ||
        vaddr + program->memory_size < vaddr)
    {
      return false;
    }
    uint64_t first = vaddr & ~(PMM_PAGE_SIZE - 1);
    uint64_t end = (vaddr + program->memory_size + PMM_PAGE_SIZE - 1) &
                   ~(PMM_PAGE_SIZE - 1);
    if (end < vaddr)
      return false;
    uint64_t flags = VMM_PAGE_USER;
    if (program->flags & ELF_FLAG_WRITABLE)
      flags |= VMM_PAGE_WRITABLE;
    if (!(program->flags & ELF_FLAG_EXECUTABLE))
      flags |= VMM_PAGE_NX;
    for (uint64_t address = first; address < end; address += PMM_PAGE_SIZE)
    {
      if (vmm_translate(address_space, address))
        continue;
      uint64_t physical = oom_alloc_user_page();
      if (!physical || !vmm_map_page(address_space, address, physical, flags))
      {
        if (physical)
          pmm_free_page(physical);
        return false;
      }
    }
    if (program->file_size &&
        !copy_to_space(address_space, vaddr, bytes + program->offset,
                       program->file_size))
      return false;
    if (first < result->lowest_address)
      result->lowest_address = first;
    if (end > result->highest_address)
      result->highest_address = end;
  }
  if (result->lowest_address == UINT64_MAX)
    return false;
  uint64_t entry = header->entry + bias;
  if (add_overflows(header->entry, bias))
    return false;
  if (entry < result->lowest_address || entry >= result->highest_address)
    return false;
  result->entry = entry;
  struct elf_dynamic_info local;
  if (dynamic == 0)
    dynamic = &local;
  memset(dynamic, 0, sizeof(*dynamic));
  dynamic->bias = bias;
  dynamic->syment_size = 24;
  dynamic->rela_entsize = ELF_RELA_WIDTH;
  if (!have_dyn)
    return true;
  if (!parse_dynamic(bytes, image_size, dyn_offset, dyn_filesz, bias, dynamic))
    return false;
  if (dynamic->rela_size && (dynamic->rela_address < result->lowest_address ||
                             dynamic->rela_address >= result->highest_address))
    return false;
  return true;
}

bool elf_load_user_image(const void *image, uint64_t image_size,
                         uint64_t address_space,
                         struct elf_load_result *result)
{
  uint16_t type = peek_type(image, image_size);
  if (type != ELF_TYPE_EXECUTABLE)
    return false;
  struct elf_dynamic_info dynamic;
  return elf_load_user_image_biased(image, image_size, address_space, 0, result,
                                    &dynamic);
}

bool elf_apply_relative_relocs(uint64_t address_space,
                               const struct elf_dynamic_info *dynamic)
{
  if (!dynamic || !dynamic->has_dynamic)
    return true;
  if (!apply_rela_table(address_space, dynamic->bias, dynamic->rela_address,
                        dynamic->rela_size, dynamic->rela_entsize))
    return false;
  if (!apply_rela_table(address_space, dynamic->bias, dynamic->jmprel_address,
                        dynamic->jmprel_size, ELF_RELA_WIDTH))
    return false;
  return true;
}

uint64_t elf_dyn_base_for_index(uint64_t index)
{
  if (add_overflows(ELF_SO_BASE, index * ELF_SO_STRIDE))
    return 0;
  uint64_t base = ELF_SO_BASE + index * ELF_SO_STRIDE;
  if (base > ELF_SO_END)
    return 0;
  return base;
}

uint16_t elf_image_type(const void *image, uint64_t image_size)
{
  return peek_type(image, image_size);
}

static bool file_vaddr_to_offset(const uint8_t *bytes, uint64_t image_size,
                                 uint64_t vaddr, uint64_t size,
                                 uint64_t *offset)
{
  if (image_size < sizeof(struct elf64_header))
    return false;
  const struct elf64_header *header = (const struct elf64_header *)bytes;
  uint64_t table_size =
      (uint64_t)header->program_count * header->program_entry_size;
  if (header->program_entry_size != sizeof(struct elf64_program_header) ||
      add_overflows(header->program_offset, table_size) ||
      header->program_offset + table_size > image_size)
    return false;
  for (uint16_t index = 0; index < header->program_count; index++)
  {
    struct elf64_program_header program;
    memcpy(&program,
           bytes + header->program_offset +
               (uint64_t)index * header->program_entry_size,
           sizeof(program));
    if (program.type != ELF_PROGRAM_LOAD)
      continue;
    if (vaddr < program.virtual_address ||
        vaddr >= program.virtual_address + program.memory_size)
      continue;
    uint64_t inner = vaddr - program.virtual_address;
    if (inner >= program.file_size || size > program.file_size - inner)
      return false;
    if (add_overflows(program.offset, inner))
      return false;
    *offset = program.offset + inner;
    return true;
  }
  return false;
}

static bool file_parse_needed(const uint8_t *bytes, uint64_t image_size,
                              struct elf_file_dyn *out)
{
  memset(out, 0, sizeof(*out));
  if (image_size < sizeof(struct elf64_header))
    return false;
  const struct elf64_header *header = (const struct elf64_header *)bytes;
  if (header->identity[0] != 0x7F || header->identity[1] != 'E' ||
      header->identity[2] != 'L' || header->identity[3] != 'F' ||
      header->identity[4] != ELF_CLASS_64 ||
      header->identity[5] != ELF_DATA_LITTLE_ENDIAN ||
      header->program_entry_size != sizeof(struct elf64_program_header))
    return false;
  uint64_t table_size =
      (uint64_t)header->program_count * header->program_entry_size;
  if (add_overflows(header->program_offset, table_size) ||
      header->program_offset + table_size > image_size)
    return false;
  bool have_dyn = false;
  uint64_t dyn_offset = 0;
  uint64_t dyn_filesz = 0;
  for (uint16_t index = 0; index < header->program_count; index++)
  {
    struct elf64_program_header program;
    memcpy(&program,
           bytes + header->program_offset +
               (uint64_t)index * header->program_entry_size,
           sizeof(program));
    if (program.type == ELF_PROGRAM_DYNAMIC)
    {
      if (have_dyn)
        return false;
      have_dyn = true;
      dyn_offset = program.offset;
      dyn_filesz = program.file_size;
    }
  }
  if (!have_dyn)
    return true;
  if (add_overflows(dyn_offset, dyn_filesz) ||
      dyn_offset + dyn_filesz > image_size ||
      dyn_filesz % ELF_DYN_TAG_WIDTH != 0)
    return false;
  uint64_t count = dyn_filesz / ELF_DYN_TAG_WIDTH;
  bool seen_null = false;
  for (uint64_t i = 0; i < count; i++)
  {
    struct elf64_dyn entry;
    memcpy(&entry, bytes + dyn_offset + i * ELF_DYN_TAG_WIDTH,
           ELF_DYN_TAG_WIDTH);
    if (entry.tag == ELF_DT_NULL)
    {
      seen_null = true;
      break;
    }
    if (entry.tag == ELF_DT_NEEDED)
    {
      if (out->needed_count >= ELF_MAX_NEEDED)
        return false;
      out->needed[out->needed_count++] = entry.value;
    }
    else if (entry.tag == ELF_DT_STRTAB)
    {
      out->strtab_vaddr = entry.value;
    }
    else if (entry.tag == ELF_DT_STRSZ)
    {
      out->strtab_size = entry.value;
    }
  }
  if (!seen_null)
    return false;
  if (out->needed_count > 0 && out->strtab_size == 0)
    return false;
  return true;
}

uint64_t elf_needed_count(const void *image, uint64_t image_size)
{
  struct elf_file_dyn info;
  if (!file_parse_needed((const uint8_t *)image, image_size, &info))
    return 0;
  return info.needed_count;
}

bool elf_needed_name(const void *image, uint64_t image_size, uint64_t index,
                     char *out, uint64_t capacity)
{
  if (!out || capacity < 2)
    return false;
  struct elf_file_dyn info;
  const uint8_t *bytes = (const uint8_t *)image;
  if (!file_parse_needed(bytes, image_size, &info))
    return false;
  if (index >= info.needed_count)
    return false;
  uint64_t offset = 0;
  if (!file_vaddr_to_offset(bytes, image_size, info.strtab_vaddr,
                            info.strtab_size, &offset))
    return false;
  if (info.needed[index] >= info.strtab_size)
    return false;
  uint64_t start = offset + info.needed[index];
  uint64_t limit = offset + info.strtab_size;
  if (start >= limit)
    return false;
  uint64_t pos = 0;
  while (start + pos < limit && pos + 1 < capacity)
  {
    char c = (char)bytes[start + pos];
    out[pos] = c;
    pos++;
    if (c == 0)
      return pos > 1;
  }
  return false;
}

static bool read_cstring_from_space(uint64_t address_space, uint64_t address,
                                    char *out, uint64_t capacity)
{
  if (!out || capacity < 2)
    return false;
  uint64_t pos = 0;
  while (pos + 1 < capacity)
  {
    uint64_t page_remain = PMM_PAGE_SIZE - (address & (PMM_PAGE_SIZE - 1));
    uint64_t take = page_remain;
    if (take > capacity - 1 - pos)
      take = capacity - 1 - pos;
    if (!copy_from_space(address_space, address, (uint8_t *)(out + pos), take))
      return false;
    for (uint64_t i = 0; i < take; i++)
    {
      if (out[pos + i] == 0)
        return pos + i > 0;
    }
    pos += take;
    address += take;
  }
  return false;
}

static bool read_sym(uint64_t address_space,
                     const struct elf_dynamic_info *dyn, uint32_t index,
                     struct elf64_sym *out)
{
  if (!dyn->symtab_address || dyn->syment_size != sizeof(struct elf64_sym))
    return false;
  if (add_overflows(dyn->symtab_address,
                    (uint64_t)index * sizeof(struct elf64_sym)))
    return false;
  return copy_from_space(address_space,
                         dyn->symtab_address +
                             (uint64_t)index * sizeof(struct elf64_sym),
                         (uint8_t *)out, sizeof(*out));
}

static bool read_hash_count(uint64_t address_space,
                            const struct elf_dynamic_info *dyn,
                            uint32_t *count)
{
  if (!dyn->hash_address)
    return false;
  uint8_t raw[8];
  if (!copy_from_space(address_space, dyn->hash_address, raw, 8))
    return false;
  uint32_t nchain = 0;
  memcpy(&nchain, raw + 4, 4);
  if (nchain == 0 || nchain > 1U << 24)
    return false;
  *count = nchain;
  return true;
}

bool elf_resolve_symbol(uint64_t address_space,
                        const struct elf_object *scope, uint64_t scope_count,
                        const char *name, uint64_t *value)
{
  if (!scope || !name || !value || !name[0] || scope_count == 0)
    return false;
  for (uint32_t pass = 0; pass < 2; pass++)
  {
    for (uint64_t o = 0; o < scope_count; o++)
    {
      const struct elf_dynamic_info *dyn = &scope[o].dyn;
      if (!dyn->has_dynamic || !dyn->symtab_address ||
          !dyn->strtab_address || !dyn->strtab_size)
        continue;
      uint32_t nchain = 0;
      if (!read_hash_count(address_space, dyn, &nchain))
        continue;
      for (uint32_t i = 1; i < nchain; i++)
      {
        struct elf64_sym sym;
        if (!read_sym(address_space, dyn, i, &sym))
          break;
        uint32_t bind = sym.info >> 4;
        if (bind != ELF_STB_GLOBAL && bind != ELF_STB_WEAK)
          continue;
        if ((pass == 0 && bind != ELF_STB_GLOBAL) ||
            (pass == 1 && bind != ELF_STB_WEAK))
          continue;
        if (sym.shndx == ELF_SHN_UNDEF)
          continue;
        if (add_overflows(sym.name, 1) ||
            (uint64_t)sym.name + 1 >= dyn->strtab_size)
          continue;
        char candidate[ELF_NAME_CAP + 32];
        if (!read_cstring_from_space(address_space,
                                     dyn->strtab_address + sym.name, candidate,
                                     sizeof(candidate)))
          continue;
        if (strcmp(candidate, name) != 0)
          continue;
        if ((uint64_t)sym.name + strlen(candidate) >= dyn->strtab_size)
          continue;
        if (sym.shndx == ELF_SHN_ABS)
          *value = sym.value;
        else
          *value = dyn->bias + sym.value;
        return true;
      }
    }
  }
  return false;
}

static bool apply_scoped_table(uint64_t address_space,
                               const struct elf_object *scope,
                               uint64_t scope_count, uint64_t self_index,
                               uint64_t table, uint64_t size)
{
  if (size == 0)
    return true;
  if (size % ELF_RELA_WIDTH != 0 || self_index >= scope_count)
    return false;
  uint64_t self_bias = scope[self_index].dyn.bias;
  const struct elf_dynamic_info *self = &scope[self_index].dyn;
  uint64_t count = size / ELF_RELA_WIDTH;
  for (uint64_t i = 0; i < count; i++)
  {
    struct elf64_rela rela;
    if (!copy_from_space(address_space, table + i * ELF_RELA_WIDTH,
                         (uint8_t *)&rela, ELF_RELA_WIDTH))
      return false;
    uint32_t type = (uint32_t)(rela.info & 0xFFFFFFFFULL);
    uint32_t sym = (uint32_t)(rela.info >> 32);
    if (type == ELF_R_X86_64_NONE)
      continue;
    if (type == ELF_R_X86_64_RELATIVE)
    {
      if (sym != 0)
        return false;
      if (rela.addend < 0 && self_bias < (uint64_t)(-(rela.addend + 1)) + 1)
        return false;
      if (!write_u64_to_space(address_space, rela.offset + self_bias,
                              self_bias + (uint64_t)rela.addend))
        return false;
      continue;
    }
    if (type != ELF_R_X86_64_GLOB_DAT && type != ELF_R_X86_64_JUMP_SLOT &&
        type != ELF_R_X86_64_64)
      return false;
    uint64_t resolved = 0;
    if (sym == 0)
    {
      if (type != ELF_R_X86_64_64)
        return false;
      if (rela.addend < 0 && self_bias < (uint64_t)(-(rela.addend + 1)) + 1)
        return false;
      resolved = self_bias + (uint64_t)rela.addend;
    }
    else
    {
      struct elf64_sym entry;
      if (!read_sym(address_space, self, sym, &entry))
        return false;
      if (entry.name >= self->strtab_size)
        return false;
      char wanted[ELF_NAME_CAP + 32];
      if (!read_cstring_from_space(address_space,
                                   self->strtab_address + entry.name, wanted,
                                   sizeof(wanted)))
        return false;
      if ((uint64_t)entry.name + strlen(wanted) >= self->strtab_size)
        return false;
      uint64_t found = 0;
      if (!elf_resolve_symbol(address_space, scope, scope_count, wanted,
                              &found))
      {
        uint32_t bind = entry.info >> 4;
        if (bind != ELF_STB_WEAK)
          return false;
        found = 0;
      }
      if (type == ELF_R_X86_64_64)
        resolved = found + (uint64_t)rela.addend;
      else
        resolved = found;
    }
    if (!write_u64_to_space(address_space, rela.offset + self_bias, resolved))
      return false;
  }
  return true;
}

bool elf_apply_relocs_with_scope(uint64_t address_space,
                                 const struct elf_object *scope,
                                 uint64_t scope_count, uint64_t self_index)
{
  if (!scope || self_index >= scope_count)
    return false;
  const struct elf_dynamic_info *dyn = &scope[self_index].dyn;
  if (!dyn->has_dynamic)
    return true;
  if (!apply_scoped_table(address_space, scope, scope_count, self_index,
                          dyn->rela_address, dyn->rela_size))
    return false;
  if (!apply_scoped_table(address_space, scope, scope_count, self_index,
                          dyn->jmprel_address, dyn->jmprel_size))
    return false;
  return true;
}
