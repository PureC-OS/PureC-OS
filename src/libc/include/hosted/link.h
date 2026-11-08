#pragma once

#include <stddef.h>
#include <stdint.h>

#define PT_NULL 0
#define PT_LOAD 1
#define PT_DYNAMIC 2
#define PT_INTERP 3
#define PT_NOTE 4
#define PT_PHDR 6
#define PT_TLS 7
#define PT_GNU_EH_FRAME 0x6474e550

typedef uint16_t Elf64_Half;
typedef uint32_t Elf64_Word;
typedef int32_t Elf64_Sword;
typedef uint64_t Elf64_Xword;
typedef int64_t Elf64_Sxword;
typedef uint64_t Elf64_Addr;
typedef uint64_t Elf64_Off;
#define ElfW(type) Elf64_##type

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} Elf64_Phdr;

struct dl_phdr_info {
    uint64_t dlpi_addr;
    const char *dlpi_name;
    const Elf64_Phdr *dlpi_phdr;
    Elf64_Half dlpi_phnum;
    uint64_t dlpi_adds;
    uint64_t dlpi_subs;
    size_t dlpi_tls_modid;
    void *dlpi_tls_data;
};

#ifdef __cplusplus
extern "C" {
#endif

int dl_iterate_phdr(int (*callback)(struct dl_phdr_info *, size_t, void *), void *data);

#ifdef __cplusplus
}
#endif
