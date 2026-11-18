#include <arch/arch.h>
#include <mm/pmm.h>

#define MMU_INDEX_MASK 0x1FF
#define MMU_LEAF_LEVEL 1

static bool nx_enabled;

static uint64_t *table_from_entry(uint64_t entry)
{
    return (uint64_t *)pmm_physical_to_virtual(entry & ARCH_PTE_ADDR_MASK);
}

static uint64_t *next_table(uint64_t *table, uint16_t index, bool create,
                            uint64_t flags)
{
    if (table[index] & ARCH_PTE_PRESENT)
        return table_from_entry(table[index]);
    if (!create)
        return 0;
    uint64_t physical = pmm_allocate_page();
    if (!physical)
        return 0;
    table[index] = physical | ARCH_PTE_PRESENT | ARCH_PTE_WRITABLE |
                   (flags & ARCH_PTE_USER);
    return (uint64_t *)pmm_physical_to_virtual(physical);
}

uint64_t arch_mmu_read_root(void)
{
    return arch_read_cr3() & ARCH_PTE_ADDR_MASK;
}

void arch_mmu_activate(uint64_t root)
{
    arch_write_cr3(root);
}

bool arch_mmu_enable_nx(void)
{
    if (!arch_cpu_has_nx())
        return false;
    uint64_t efer = arch_rdmsr(ARCH_MSR_EFER);
    efer |= 1ULL << 11;
    arch_wrmsr(ARCH_MSR_EFER, efer);
    nx_enabled = true;
    return true;
}

bool arch_mmu_nx_enabled(void)
{
    return nx_enabled;
}

void arch_mmu_enable_write_protect(void)
{
    uint64_t value = arch_read_cr0();
    if (!(value & (1ULL << 16)))
        arch_write_cr0(value | (1ULL << 16));
}

uint64_t arch_mmu_nx_bits(uint64_t flags)
{
    return nx_enabled ? flags & ARCH_PTE_NX : 0;
}

uint64_t *arch_mmu_walk(uint64_t root, uint64_t virtual_address, bool create,
                        uint64_t flags)
{
    uint64_t *pml4 = (uint64_t *)pmm_physical_to_virtual(root);
    uint64_t *pdpt = next_table(pml4, (virtual_address >> 39) & MMU_INDEX_MASK,
                                create, flags);
    if (!pdpt)
        return 0;
    uint64_t *pd = next_table(pdpt, (virtual_address >> 30) & MMU_INDEX_MASK,
                              create, flags);
    if (!pd)
        return 0;
    uint64_t *pt = next_table(pd, (virtual_address >> 21) & MMU_INDEX_MASK,
                              create, flags);
    if (!pt)
        return 0;
    return &pt[(virtual_address >> 12) & MMU_INDEX_MASK];
}

uint64_t *arch_mmu_walk_skip(uint64_t root, uint64_t virtual_address,
                             uint64_t *next)
{
    uint64_t *pml4 = (uint64_t *)pmm_physical_to_virtual(root);
    uint64_t *pdpt =
        next_table(pml4, (virtual_address >> 39) & MMU_INDEX_MASK, false, 0);
    if (!pdpt) {
        *next = (virtual_address | ((1ULL << 39) - 1)) + 1;
        return 0;
    }
    uint64_t *pd =
        next_table(pdpt, (virtual_address >> 30) & MMU_INDEX_MASK, false, 0);
    if (!pd) {
        *next = (virtual_address | ((1ULL << 30) - 1)) + 1;
        return 0;
    }
    uint64_t *pt =
        next_table(pd, (virtual_address >> 21) & MMU_INDEX_MASK, false, 0);
    if (!pt) {
        *next = (virtual_address | ((1ULL << 21) - 1)) + 1;
        return 0;
    }
    *next = virtual_address + ARCH_MMU_PAGE_SIZE;
    return &pt[(virtual_address >> 12) & MMU_INDEX_MASK];
}

uint64_t arch_mmu_root_new(uint64_t kernel_root)
{
    uint64_t physical = pmm_allocate_page();
    if (!physical)
        return 0;
    uint64_t *destination = (uint64_t *)pmm_physical_to_virtual(physical);
    uint64_t *kernel = (uint64_t *)pmm_physical_to_virtual(kernel_root);
    for (uint16_t index = 256; index < 512; index++)
        destination[index] = kernel[index];
    return physical;
}

static void destroy_table_level(uint64_t physical, unsigned level,
                                const struct arch_mmu_leaf_ops *ops)
{
    uint64_t *table = (uint64_t *)pmm_physical_to_virtual(physical);
    for (uint16_t index = 0; index < 512; index++) {
        if (!(table[index] & ARCH_PTE_PRESENT)) {
            if (level == MMU_LEAF_LEVEL)
                ops->release(table[index]);
            continue;
        }
        if (level == MMU_LEAF_LEVEL)
            ops->release(table[index]);
        else
            destroy_table_level(table[index] & ARCH_PTE_ADDR_MASK, level - 1,
                                ops);
    }
    pmm_free_page(physical);
}

static uint64_t clone_table_level(uint64_t source_physical, unsigned level,
                                  const struct arch_mmu_leaf_ops *ops)
{
    uint64_t physical = pmm_allocate_page();
    if (!physical)
        return 0;
    uint64_t *source = (uint64_t *)pmm_physical_to_virtual(source_physical);
    uint64_t *destination = (uint64_t *)pmm_physical_to_virtual(physical);
    for (uint16_t index = 0; index < 512; index++) {
        uint64_t entry = source[index];
        if (level == MMU_LEAF_LEVEL) {
            if (!ops->clone(&source[index], &destination[index])) {
                destroy_table_level(physical, level, ops);
                return 0;
            }
            continue;
        }
        if (!(entry & ARCH_PTE_PRESENT))
            continue;
        uint64_t child =
            clone_table_level(entry & ARCH_PTE_ADDR_MASK, level - 1, ops);
        if (!child) {
            destroy_table_level(physical, level, ops);
            return 0;
        }
        destination[index] = child | (entry & ~ARCH_PTE_ADDR_MASK);
    }
    return physical;
}

bool arch_mmu_clone_user(uint64_t source, uint64_t destination,
                         const struct arch_mmu_leaf_ops *ops)
{
    uint64_t *src = (uint64_t *)pmm_physical_to_virtual(source);
    uint64_t *dst = (uint64_t *)pmm_physical_to_virtual(destination);
    for (uint16_t index = 0; index < ARCH_MMU_USER_ROOT_ENTRIES; index++) {
        if (!(src[index] & ARCH_PTE_PRESENT))
            continue;
        uint64_t child = clone_table_level(src[index] & ARCH_PTE_ADDR_MASK, 3, ops);
        if (!child)
            return false;
        dst[index] = child | (src[index] & ~ARCH_PTE_ADDR_MASK);
    }
    return true;
}

void arch_mmu_detach_user(uint64_t root, struct arch_mmu_detached *detached)
{
    uint64_t *table = (uint64_t *)pmm_physical_to_virtual(root);
    for (unsigned i = 0; i < ARCH_MMU_USER_ROOT_ENTRIES; i++) {
        detached->entry[i] = table[i];
        table[i] = 0;
    }
}

void arch_mmu_free_detached(struct arch_mmu_detached *detached,
                            const struct arch_mmu_leaf_ops *ops)
{
    for (uint16_t index = 0; index < ARCH_MMU_USER_ROOT_ENTRIES; index++) {
        if (!(detached->entry[index] & ARCH_PTE_PRESENT))
            continue;
        destroy_table_level(detached->entry[index] & ARCH_PTE_ADDR_MASK, 3, ops);
    }
}

uint64_t arch_mmu_count_user_pages(uint64_t root)
{
    uint64_t count = 0;
    uint64_t *pml4 = (uint64_t *)pmm_physical_to_virtual(root);
    for (uint16_t pml4_index = 0; pml4_index < ARCH_MMU_USER_ROOT_ENTRIES;
         pml4_index++) {
        if (!(pml4[pml4_index] & ARCH_PTE_PRESENT))
            continue;
        uint64_t *pdpt = table_from_entry(pml4[pml4_index]);
        for (uint16_t pdpt_index = 0; pdpt_index < 512; pdpt_index++) {
            if (!(pdpt[pdpt_index] & ARCH_PTE_PRESENT))
                continue;
            uint64_t *pd = table_from_entry(pdpt[pdpt_index]);
            for (uint16_t pd_index = 0; pd_index < 512; pd_index++) {
                if (!(pd[pd_index] & ARCH_PTE_PRESENT))
                    continue;
                uint64_t *pt = table_from_entry(pd[pd_index]);
                for (uint16_t pt_index = 0; pt_index < 512; pt_index++)
                    if ((pt[pt_index] & (ARCH_PTE_PRESENT | ARCH_PTE_USER)) ==
                        (ARCH_PTE_PRESENT | ARCH_PTE_USER))
                        count++;
            }
        }
    }
    return count;
}
