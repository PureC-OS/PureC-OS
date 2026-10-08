#pragma once

#include <stdbool.h>
#include <stdint.h>

#define ELF_USER_MIN 0x0000000000400000ULL
#define ELF_USER_MAX 0x0000700000000000ULL
#define ELF_PIE_DEFAULT_BASE 0x0000555555554000ULL
#define ELF_SO_BASE 0x0000600000000000ULL
#define ELF_SO_END 0x00006FFFFFFFFFFFULL
#define ELF_PIE_ASLR_SPAN 0x0000000040000000ULL

#define ELF_TYPE_SHARED 3
#define ELF_PROGRAM_DYNAMIC 2

#define ELF_DT_NULL 0
#define ELF_DT_NEEDED 1
#define ELF_DT_STRTAB 5
#define ELF_DT_SYMTAB 6
#define ELF_DT_RELA 7
#define ELF_DT_RELASZ 8
#define ELF_DT_RELAENT 9
#define ELF_DT_STRSZ 10
#define ELF_DT_SYMENT 11
#define ELF_DT_SONAME 14
#define ELF_DT_JMPREL 23
#define ELF_DT_PLTRELSZ 2
#define ELF_DT_GNU_HASH 0x6FFFFEF5ULL
#define ELF_DT_RELACOUNT 0x6FFFFFF9ULL

#define ELF_R_X86_64_NONE 0
#define ELF_R_X86_64_64 1
#define ELF_R_X86_64_GLOB_DAT 6
#define ELF_R_X86_64_JUMP_SLOT 7
#define ELF_R_X86_64_RELATIVE 8

struct elf_load_result
{
  uint64_t entry;
  uint64_t lowest_address;
  uint64_t highest_address;
};

struct elf_dynamic_info
{
  uint64_t bias;
  uint64_t rela_address;
  uint64_t rela_size;
  uint64_t rela_entsize;
  uint64_t jmprel_address;
  uint64_t jmprel_size;
  uint64_t symtab_address;
  uint64_t strtab_address;
  uint64_t strtab_size;
  uint64_t syment_size;
  uint64_t relacount;
  bool has_dynamic;
};

bool elf_load_user_image(const void *image, uint64_t image_size,
                         uint64_t address_space,
                         struct elf_load_result *result);
bool elf_load_user_image_biased(const void *image, uint64_t image_size,
                                uint64_t address_space, uint64_t bias,
                                struct elf_load_result *result,
                                struct elf_dynamic_info *dynamic);
bool elf_apply_relative_relocs(uint64_t address_space,
                               const struct elf_dynamic_info *dynamic);
uint64_t elf_dyn_base_for_index(uint64_t index);
