#pragma once

#include <stdbool.h>
#include <stdint.h>

struct vma_table;

struct vma_table *vma_table_create(void);
void vma_table_destroy(struct vma_table *table);
struct vma_table *vma_table_clone(const struct vma_table *source);
int vma_map(struct vma_table *table, uint64_t start, uint64_t end,
            uint32_t prot);
int vma_unmap(struct vma_table *table, uint64_t start, uint64_t end);
int vma_set_prot(struct vma_table *table, uint64_t start, uint64_t end,
                 uint32_t prot);
bool vma_lookup(struct vma_table *table, uint64_t address, uint32_t *prot,
                uint64_t *start, uint64_t *end);
bool vma_range_free(struct vma_table *table, uint64_t start, uint64_t end);
uint64_t vma_find_gap(struct vma_table *table, uint64_t length, uint64_t low,
                      uint64_t high);
uint32_t vma_count(struct vma_table *table);
