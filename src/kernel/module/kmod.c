// Runtime loader for relocatable (ET_REL, x86-64) kernel modules.
//
// Address plan: modules live in a reserved higher-half area
// [KMOD_AREA_BASE, KMOD_AREA_END), allocated with a page-granular
// bump allocator and mapped RW (executable: NX bit is not set,
// same posture as the main kernel image which links a RWX segment).
// Physical pages may be scattered; only virtual contiguity matters.
// No unloading in v1: pages are never reclaimed.
#include "kmod.h"
#include "../diagnostics/klog.h"
#include "../../mm/pmm.h"
#include "../../mm/vmm.h"
#include "../../lib/string.h"
#include "../../boot/install_source.h"

#define KMOD_MAX 16u
#define KMOD_AREA_BASE 0xFFFFFFFFC0000000ULL
#define KMOD_AREA_END 0xFFFFFFFFD0000000ULL
#define KMOD_MAX_SECTIONS 256u

#define ELF_TYPE_REL 1u
#define ELF_MACHINE_X86_64 62u

#define SHT_SYMTAB 2u
#define SHT_RELA 4u
#define SHT_NOBITS 8u

#define SHF_ALLOC 2u

#define SHN_UNDEF 0u
#define SHN_ABS 0xFFF1u
#define SHN_COMMON 0xFFF2u

#define STB_GLOBAL 1u
#define STT_OBJECT 1u
#define STT_FUNC 2u

#define R_X86_64_NONE 0u
#define R_X86_64_64 1u
#define R_X86_64_PC32 2u
#define R_X86_64_PLT32 4u
#define R_X86_64_GLOB_DAT 6u
#define R_X86_64_JUMP_SLOT 7u
#define R_X86_64_RELATIVE 8u
#define R_X86_64_32 10u
#define R_X86_64_32S 11u

struct elf64_header {
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

struct elf64_section {
    uint32_t name;
    uint32_t type;
    uint64_t flags;
    uint64_t address;
    uint64_t offset;
    uint64_t size;
    uint32_t link;
    uint32_t info;
    uint64_t alignment;
    uint64_t entry_size;
} __attribute__((packed));

struct elf64_sym {
    uint32_t name;
    uint8_t info;
    uint8_t other;
    uint16_t section;
    uint64_t value;
    uint64_t size;
} __attribute__((packed));

struct elf64_rela {
    uint64_t offset;
    uint64_t info;
    int64_t addend;
} __attribute__((packed));

struct kmod {
    char name[64];
    uint64_t base;
    uint64_t size;
    const struct kmod_info *info;
    // symtab kept for kmod_get_symbol()
    const struct elf64_sym *syms;
    uint64_t sym_count;
    const char *strtab;
    uint64_t strtab_size;
    // section index -> loaded offset (only SHF_ALLOC sections set)
    uint64_t sec_vaddr[KMOD_MAX_SECTIONS];
    // section index -> 1 when the section was loaded (SHF_ALLOC)
    uint8_t sec_loaded[KMOD_MAX_SECTIONS];
    uint16_t sec_count;
    bool ready;
};

static struct kmod modules[KMOD_MAX];
static unsigned module_count = 0;
static uint64_t area_next = KMOD_AREA_BASE;

void kmod_init(void) {
    memset(modules, 0, sizeof(modules));
    module_count = 0;
    area_next = KMOD_AREA_BASE;
    klogf(KLOG_INFO, "kmod: module area [0x%llx, 0x%llx), max %u modules",
          (unsigned long long)KMOD_AREA_BASE,
          (unsigned long long)KMOD_AREA_END, KMOD_MAX);
}

static bool add_overflows_u64(uint64_t a, uint64_t b) {
    return a > UINT64_MAX - b;
}

// Bounded NUL-terminated string inside a table. Returns NULL when the
// offset is out of range or no NUL follows within the table.
static const char *tab_string(const char *tab, uint64_t tab_size, uint64_t off) {
    if (!tab || off >= tab_size)
        return NULL;
    const char *s = tab + off;
    for (uint64_t i = off; i < tab_size; i++) {
        if (tab[i] == '\0')
            return s;
    }
    return NULL;
}

int kmod_load_image(const char *name, const void *image, uint64_t size) {
    if (!name || !image || size < sizeof(struct elf64_header)) {
        klog(KLOG_ERROR, "kmod: bad load arguments");
        return -1;
    }
    if (module_count >= KMOD_MAX) {
        klog(KLOG_ERROR, "kmod: module table full");
        return -1;
    }
    const uint8_t *bytes = (const uint8_t *)image;
    const struct elf64_header *eh = (const struct elf64_header *)image;
    if (eh->identity[0] != 0x7F || eh->identity[1] != 'E' ||
        eh->identity[2] != 'L' || eh->identity[3] != 'F' ||
        eh->identity[4] != 2 || eh->identity[5] != 1 ||
        eh->type != ELF_TYPE_REL || eh->machine != ELF_MACHINE_X86_64 ||
        eh->section_entry_size < sizeof(struct elf64_section)) {
        klogf(KLOG_ERROR, "kmod: '%s' is not a 64-bit x86-64 ET_REL image", name);
        return -1;
    }
    uint16_t shnum = eh->section_count;
    if (shnum == 0) {
        // Extended numbering: real count lives in shdr[0].sh_size.
        if (eh->section_offset + sizeof(struct elf64_section) > size) {
            klog(KLOG_ERROR, "kmod: truncated section header 0");
            return -1;
        }
        const struct elf64_section *sh0 =
            (const struct elf64_section *)(bytes + eh->section_offset);
        if (sh0->size == 0 || sh0->size > KMOD_MAX_SECTIONS) {
            klog(KLOG_ERROR, "kmod: insane extended section count");
            return -1;
        }
        shnum = (uint16_t)sh0->size;
    }
    if (shnum > KMOD_MAX_SECTIONS) {
        klog(KLOG_ERROR, "kmod: too many sections");
        return -1;
    }
    uint64_t sh_table_size = (uint64_t)shnum * eh->section_entry_size;
    if (add_overflows_u64(eh->section_offset, sh_table_size) ||
        eh->section_offset + sh_table_size > size) {
        klog(KLOG_ERROR, "kmod: section table out of image");
        return -1;
    }

    // Locate .symtab + linked .strtab.
    const struct elf64_section *symtab_sh = NULL;
    for (uint16_t i = 0; i < shnum; i++) {
        const struct elf64_section *s =
            (const struct elf64_section *)(bytes + eh->section_offset +
                                            (uint64_t)i * eh->section_entry_size);
        if (s->type == SHT_SYMTAB && s->entry_size == sizeof(struct elf64_sym)) {
            symtab_sh = s;
            break;
        }
    }
    if (!symtab_sh) {
        klogf(KLOG_ERROR, "kmod: '%s' has no symtab, refusing to load", name);
        return -1;
    }
    // The strtab linked from .symtab (sh_link).
    const struct elf64_section *linked_str = NULL;
    {
        uint32_t link = symtab_sh->link;
        if (link < shnum) {
            linked_str =
                (const struct elf64_section *)(bytes + eh->section_offset +
                                                (uint64_t)link * eh->section_entry_size);
        }
    }
    if (!linked_str || add_overflows_u64(linked_str->offset, linked_str->size) ||
        linked_str->offset + linked_str->size > size) {
        klog(KLOG_ERROR, "kmod: symtab strtab out of image");
        return -1;
    }
    uint64_t sym_count = symtab_sh->size / sizeof(struct elf64_sym);
    if (add_overflows_u64(symtab_sh->offset, symtab_sh->size) ||
        symtab_sh->offset + symtab_sh->size > size || sym_count == 0) {
        klog(KLOG_ERROR, "kmod: symtab out of image");
        return -1;
    }
    const struct elf64_sym *syms =
        (const struct elf64_sym *)(bytes + symtab_sh->offset);
    const char *strtab = (const char *)(bytes + linked_str->offset);

    // Layout SHF_ALLOC sections.
    static uint64_t sec_vaddr[KMOD_MAX_SECTIONS];
    static uint64_t sec_memsz[KMOD_MAX_SECTIONS];
    uint64_t image_size = 0;
    for (uint16_t i = 0; i < shnum; i++) {
        sec_vaddr[i] = 0;
        sec_memsz[i] = 0;
        const struct elf64_section *s =
            (const struct elf64_section *)(bytes + eh->section_offset +
                                            (uint64_t)i * eh->section_entry_size);
        if (!(s->flags & SHF_ALLOC))
            continue;
        if (s->type != SHT_NOBITS &&
            (add_overflows_u64(s->offset, s->size) || s->offset + s->size > size)) {
            klogf(KLOG_ERROR, "kmod: section %u data out of image", i);
            return -1;
        }
        uint64_t align = s->alignment ? s->alignment : 1;
        if (align & (align - 1)) {
            klogf(KLOG_ERROR, "kmod: section %u has non-power-of-2 alignment", i);
            return -1;
        }
        uint64_t mask = align - 1;
        if (add_overflows_u64(image_size, mask)) {
            klog(KLOG_ERROR, "kmod: layout overflow");
            return -1;
        }
        image_size = (image_size + mask) & ~mask;
        sec_vaddr[i] = image_size;
        sec_memsz[i] = s->size;
        if (add_overflows_u64(image_size, s->size)) {
            klog(KLOG_ERROR, "kmod: layout overflow");
            return -1;
        }
        image_size += s->size;
    }
    if (image_size == 0) {
        klog(KLOG_ERROR, "kmod: image has no allocatable sections");
        return -1;
    }
    uint64_t pages = (image_size + PMM_PAGE_SIZE - 1) / PMM_PAGE_SIZE;
    uint64_t map_size = pages * PMM_PAGE_SIZE;
    if (add_overflows_u64(area_next, map_size) || area_next + map_size > KMOD_AREA_END) {
        klog(KLOG_ERROR, "kmod: module area exhausted");
        return -1;
    }
    uint64_t base = area_next;
    uint64_t kas = vmm_kernel_address_space();
    if (!vmm_map_new_pages(kas, base, pages, VMM_PAGE_WRITABLE)) {
        klog(KLOG_ERROR, "kmod: failed to map module pages");
        return -1;
    }

    // Copy sections, zero BSS.
    for (uint16_t i = 0; i < shnum; i++) {
        const struct elf64_section *s =
            (const struct elf64_section *)(bytes + eh->section_offset +
                                            (uint64_t)i * eh->section_entry_size);
        if (!(s->flags & SHF_ALLOC))
            continue;
        void *dst = (void *)(uintptr_t)(base + sec_vaddr[i]);
        if (s->type == SHT_NOBITS) {
            memset(dst, 0, s->size);
        } else if (s->size) {
            memcpy(dst, bytes + s->offset, s->size);
        }
    }

    // Apply RELA relocations. Only RELA sections targeting SHF_ALLOC
    // sections matter (debug/info relocations are skipped).
    uint64_t bias = base; // ET_REL (ld -r) section VMAs start at 0
    for (uint16_t i = 0; i < shnum; i++) {
        const struct elf64_section *rsh =
            (const struct elf64_section *)(bytes + eh->section_offset +
                                            (uint64_t)i * eh->section_entry_size);
        if (rsh->type != SHT_RELA || rsh->entry_size != sizeof(struct elf64_rela))
            continue;
        const struct elf64_section *tsh = NULL;
        if (rsh->info < shnum) {
            tsh = (const struct elf64_section *)(bytes + eh->section_offset +
                                                  (uint64_t)rsh->info * eh->section_entry_size);
        }
        if (!tsh || !(tsh->flags & SHF_ALLOC))
            continue;
        uint64_t count = rsh->size / sizeof(struct elf64_rela);
        if (add_overflows_u64(rsh->offset, rsh->size) || rsh->offset + rsh->size > size) {
            klog(KLOG_ERROR, "kmod: rela section out of image");
            return -1;
        }
        const struct elf64_rela *relas =
            (const struct elf64_rela *)(bytes + rsh->offset);
        uint64_t tbase = base + sec_vaddr[rsh->info];
        uint64_t tsize = sec_memsz[rsh->info];
        for (uint64_t r = 0; r < count; r++) {
            uint32_t type = (uint32_t)(relas[r].info & 0xFFFFFFFFu);
            uint32_t sym = (uint32_t)(relas[r].info >> 32);
            uint64_t off = relas[r].offset;
            if (off + 8 > tsize) {
                klog(KLOG_ERROR, "kmod: relocation offset out of target");
                return -1;
            }
            uint64_t place = tbase + off;
            uint64_t symval = 0;
            if (sym != 0) {
                if (sym >= sym_count) {
                    klog(KLOG_ERROR, "kmod: relocation sym index out of range");
                    return -1;
                }
                const struct elf64_sym *st = &syms[sym];
                uint16_t bind = st->info >> 4;
                (void)bind;
                if (st->section == SHN_UNDEF) {
                    const char *sname =
                        tab_string(strtab, linked_str->size, st->name);
                    if (!sname) {
                        klog(KLOG_ERROR, "kmod: bad undefined symbol name");
                        return -1;
                    }
                    symval = ksym_lookup(sname);
                    if (!symval) {
                        klogf(KLOG_ERROR, "kmod: unresolved kernel symbol '%s'", sname);
                        return -1;
                    }
                } else if (st->section == SHN_ABS) {
                    symval = st->value;
                } else if (st->section == SHN_COMMON) {
                    klog(KLOG_ERROR, "kmod: COMMON symbols unsupported, rebuild module");
                    return -1;
                } else {
                    if (st->section >= shnum) {
                        klog(KLOG_ERROR, "kmod: defined sym section out of range");
                        return -1;
                    }
                    symval = base + sec_vaddr[st->section] + st->value;
                }
            }
            int64_t addend = relas[r].addend;
            switch (type) {
            case R_X86_64_NONE:
                break;
            case R_X86_64_64:
                *(uint64_t *)(uintptr_t)place = symval + (uint64_t)addend;
                break;
            case R_X86_64_GLOB_DAT:
            case R_X86_64_JUMP_SLOT:
                *(uint64_t *)(uintptr_t)place = symval;
                break;
            case R_X86_64_RELATIVE:
                *(uint64_t *)(uintptr_t)place = bias + (uint64_t)addend;
                break;
            case R_X86_64_PC32:
            case R_X86_64_PLT32: {
                // PLT32 needs no PLT here: direct static call/jump.
                int64_t v = (int64_t)symval + addend - (int64_t)place;
                if (v < INT32_MIN || v > INT32_MAX) {
                    klog(KLOG_ERROR, "kmod: PC32 relocation out of range");
                    return -1;
                }
                *(int32_t *)(uintptr_t)place = (int32_t)v;
                break;
            }
            case R_X86_64_32: {
                uint64_t v = symval + (uint64_t)addend;
                if (v > UINT32_MAX) {
                    klog(KLOG_ERROR, "kmod: R32 relocation overflows 32 bits");
                    return -1;
                }
                *(uint32_t *)(uintptr_t)place = (uint32_t)v;
                break;
            }
            case R_X86_64_32S: {
                int64_t v = (int64_t)symval + addend;
                if (v < INT32_MIN || v > INT32_MAX) {
                    klog(KLOG_ERROR, "kmod: R32S relocation out of range");
                    return -1;
                }
                *(int32_t *)(uintptr_t)place = (int32_t)v;
                break;
            }
            default:
                klogf(KLOG_ERROR, "kmod: unsupported relocation type %u", type);
                return -1;
            }
        }
    }

    // Find and validate the module descriptor.
    const struct kmod_info *info = NULL;
    for (uint64_t i = 1; i < sym_count; i++) {
        if (syms[i].section == SHN_UNDEF || syms[i].section >= shnum)
            continue;
        const char *sname = tab_string(strtab, linked_str->size, syms[i].name);
        if (!sname || strcmp(sname, "kmod_info") != 0)
            continue;
        if (syms[i].size < sizeof(struct kmod_info)) {
            klog(KLOG_ERROR, "kmod: kmod_info descriptor too small");
            return -1;
        }
        info = (const struct kmod_info *)(uintptr_t)(base + sec_vaddr[syms[i].section] +
                                                      syms[i].value);
        break;
    }
    if (!info) {
        klogf(KLOG_ERROR, "kmod: '%s' exports no kmod_info descriptor", name);
        return -1;
    }
    if (info->abi != KMOD_ABI_VERSION || !info->name) {
        klog(KLOG_ERROR, "kmod: kmod_info ABI mismatch");
        return -1;
    }
    // The name string lives in module memory: make sure it is mapped
    // and NUL-terminated before trusting it (corrupt ISO modules
    // must not fault the kernel here).
    {
        uint64_t kas_check = vmm_kernel_address_space();
        const char *nm = info->name;
        uint64_t i = 0;
        for (; i < 64; i++) {
            if (!vmm_translate(kas_check, (uint64_t)(uintptr_t)(nm + i)))
                break;
            if (nm[i] == '\0')
                break;
        }
        if (i == 0 || i == 64 || nm[i] != '\0') {
            klog(KLOG_ERROR, "kmod: module name string invalid");
            return -1;
        }
    }
    const char *modname = info->name;

    struct kmod *m = &modules[module_count];
    memset(m, 0, sizeof(*m));
    strncpy(m->name, modname, sizeof(m->name) - 1);
    m->base = base;
    m->size = map_size;
    m->info = info;
    m->syms = syms;
    m->sym_count = sym_count;
    m->strtab = strtab;
    m->strtab_size = linked_str->size;
    m->sec_count = shnum;
    for (uint16_t i = 0; i < shnum; i++) {
        m->sec_vaddr[i] = sec_vaddr[i];
        const struct elf64_section *s =
            (const struct elf64_section *)(bytes + eh->section_offset +
                                            (uint64_t)i * eh->section_entry_size);
        m->sec_loaded[i] = (s->flags & SHF_ALLOC) ? 1 : 0;
    }

    if (info->init) {
        klogf(KLOG_INFO, "kmod: calling %s:init()", modname);
        int rc = info->init();
        if (rc != 0) {
            klogf(KLOG_ERROR, "kmod: %s:init() failed (rc=%d)", modname, rc);
            memset(m, 0, sizeof(*m));
            return -1;
        }
    }
    m->ready = true;
    module_count++;
    area_next = base + map_size;
    klogf(KLOG_OK, "kmod: '%s' loaded at 0x%llx (%llu KB)", modname,
          (unsigned long long)base, (unsigned long long)map_size / 1024);
    return 0;
}

int kmod_load_limine(const char *limine_path) {
    if (!limine_path) {
        klog(KLOG_ERROR, "kmod: NULL module path");
        return -1;
    }
    const void *image = NULL;
    uint64_t size = 0;
    if (!boot_get_module(limine_path, &image, &size) || !image || !size) {
        klogf(KLOG_ERROR, "kmod: Limine module '%s' not found", limine_path);
        return -1;
    }
    const char *name = limine_path;
    for (const char *c = limine_path; *c; c++) {
        if (*c == '/' && c[1])
            name = c + 1;
    }
    return kmod_load_image(name, image, size);
}

void *kmod_get_symbol(const char *modname, const char *symname) {
    if (!modname || !symname)
        return NULL;
    for (unsigned m = 0; m < module_count; m++) {
        if (!modules[m].ready || strcmp(modules[m].name, modname) != 0)
            continue;
        for (uint64_t i = 1; i < modules[m].sym_count; i++) {
            const struct elf64_sym *st = &modules[m].syms[i];
            if (st->section == SHN_UNDEF || st->section == SHN_ABS ||
                st->section == SHN_COMMON || st->section >= modules[m].sec_count)
                continue;
            const char *sname =
                tab_string(modules[m].strtab, modules[m].strtab_size, st->name);
            if (!sname || strcmp(sname, symname) != 0)
                continue;
            uint8_t stype = st->info & 0x0F;
            if (stype != STT_FUNC && stype != STT_OBJECT)
                continue;
            if (st->section == 0 || !modules[m].sec_loaded[st->section])
                continue; // NULL section or never loaded (debug/info)
            return (void *)(uintptr_t)(modules[m].base +
                                        modules[m].sec_vaddr[st->section] +
                                        st->value);
        }
    }
    return NULL;
}

void kmod_list(void) {
    klogf(KLOG_INFO, "kmod: %u module(s) loaded", module_count);
    for (unsigned m = 0; m < module_count; m++) {
        klogf(KLOG_INFO, "kmod:   %s base=0x%llx size=%llu KB %s", modules[m].name,
              (unsigned long long)modules[m].base,
              (unsigned long long)modules[m].size / 1024,
              modules[m].ready ? "ready" : "failed");
    }
}
