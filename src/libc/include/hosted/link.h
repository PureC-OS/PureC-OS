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
    uint16_t dlpi_phnum;
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
