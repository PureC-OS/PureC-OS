#pragma once

#include <stdbool.h>
#include <stdint.h>

#define VMM_PAGE_PRESENT  (1ULL<<0)
#define VMM_PAGE_WRITABLE (1ULL<<1)
#define VMM_PAGE_USER     (1ULL<<2)
#define VMM_PAGE_NX       (1ULL<<63)

void vmm_init(void);
uint64_t vmm_kernel_address_space(void);
uint64_t vmm_create_address_space(void);
void vmm_destroy_address_space(uint64_t address_space);
bool vmm_map_page(uint64_t address_space, uint64_t virtual_address,
                  uint64_t physical_address, uint64_t flags);
bool vmm_map_new_pages(uint64_t address_space, uint64_t virtual_address,
                       uint64_t page_count, uint64_t flags);
uint64_t vmm_translate(uint64_t address_space, uint64_t virtual_address);
bool vmm_user_range_accessible(uint64_t address_space, uint64_t address,
                               uint64_t size, bool writable);
uint64_t vmm_user_page_count(uint64_t address_space);
void vmm_switch_address_space(uint64_t address_space);
