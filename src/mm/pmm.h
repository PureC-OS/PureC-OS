#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "../boot/limine.h"

#define PMM_PAGE_SIZE 4096ULL

void pmm_init(const struct limine_memmap_response *memory_map,
               uint64_t hhdm_offset);
uint64_t pmm_allocate_page(void);
uint64_t pmm_allocate_contiguous(uint64_t page_count);
void pmm_free_contiguous(uint64_t physical_address, uint64_t page_count);
void pmm_free_page(uint64_t physical_address);
void *pmm_physical_to_virtual(uint64_t physical_address);
uint64_t pmm_total_bytes(void);
uint64_t pmm_free_bytes(void);
bool pmm_is_ready(void);
