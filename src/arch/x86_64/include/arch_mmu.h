#pragma once
#include <stdbool.h>
#include <stdint.h>

#define ARCH_PTE_PRESENT (1ULL << 0)
#define ARCH_PTE_WRITABLE (1ULL << 1)
#define ARCH_PTE_USER (1ULL << 2)
#define ARCH_PTE_PWT (1ULL << 3)
#define ARCH_PTE_PCD (1ULL << 4)
#define ARCH_PTE_PAT (1ULL << 7)
#define ARCH_PTE_ACCESSED (1ULL << 5)
#define ARCH_PTE_COW (1ULL << 9)
#define ARCH_PTE_SWAPENT (1ULL << 10)
#define ARCH_PTE_ANON (1ULL << 11)
#define ARCH_PTE_SHARED (1ULL << 52)
#define ARCH_PTE_DEVICE (1ULL << 53)
#define ARCH_PTE_NX (1ULL << 63)
#define ARCH_PTE_ADDR_MASK 0x000FFFFFFFFFF000ULL
#define ARCH_MMU_PAGE_SIZE 4096ULL
#define ARCH_MMU_USER_TOP 0x0000800000000000ULL
#define ARCH_MMU_USER_ROOT_ENTRIES 256

struct arch_mmu_detached {
    uint64_t entry[ARCH_MMU_USER_ROOT_ENTRIES];
};

struct arch_mmu_leaf_ops {
    bool (*clone)(uint64_t *source, uint64_t *destination);
    void (*release)(uint64_t entry);
};

uint64_t arch_mmu_read_root(void);
void arch_mmu_activate(uint64_t root);
bool arch_mmu_enable_nx(void);
bool arch_mmu_nx_enabled(void);
void arch_mmu_enable_write_protect(void);
uint64_t arch_mmu_nx_bits(uint64_t flags);
uint64_t *arch_mmu_walk(uint64_t root, uint64_t virtual_address, bool create,
                        uint64_t flags);
uint64_t *arch_mmu_walk_skip(uint64_t root, uint64_t virtual_address,
                             uint64_t *next);
uint64_t arch_mmu_root_new(uint64_t kernel_root);
bool arch_mmu_clone_user(uint64_t source, uint64_t destination,
                         const struct arch_mmu_leaf_ops *ops);
void arch_mmu_detach_user(uint64_t root, struct arch_mmu_detached *detached);
void arch_mmu_free_detached(struct arch_mmu_detached *detached,
                            const struct arch_mmu_leaf_ops *ops);
uint64_t arch_mmu_count_user_pages(uint64_t root);
