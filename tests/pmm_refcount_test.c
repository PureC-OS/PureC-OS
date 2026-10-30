#include "mm/pmm.h"
#include "boot/limine.h"
#include "kernel/diagnostics/klog.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define PAGES 256
void klogf(enum klog_level level, const char *format, ...) { (void)level; (void)format; }

int main(void) {
    void *pool = aligned_alloc(PMM_PAGE_SIZE, PAGES * PMM_PAGE_SIZE);
    assert(pool);
    struct limine_memmap_entry entry = {.base = PMM_PAGE_SIZE, .length = PAGES * PMM_PAGE_SIZE, .type = LIMINE_MEMMAP_USABLE};
    struct limine_memmap_entry *entries[] = {&entry};
    struct limine_memmap_response map = {.entry_count = 1, .entries = entries};
    pmm_init(&map, (uint64_t)(uintptr_t)pool - PMM_PAGE_SIZE);
    uint64_t initial = pmm_free_bytes();

    uint64_t page = pmm_allocate_page();
    assert(page);
    assert(pmm_frame_shared(page) == 0);
    assert(pmm_frame_ref(page));
    assert(pmm_frame_ref(page));
    assert(pmm_frame_shared(page) == 2);
    pmm_free_page(page);
    assert(pmm_frame_shared(page) == 1);
    assert(pmm_free_bytes() == initial - PMM_PAGE_SIZE);
    pmm_free_page(page);
    assert(pmm_frame_shared(page) == 0);
    assert(pmm_free_bytes() == initial - PMM_PAGE_SIZE);
    pmm_free_page(page);
    assert(pmm_free_bytes() == initial);

    assert(!pmm_frame_ref(page));
    assert(!pmm_frame_ref(0));
    assert(!pmm_frame_ref(PAGES * 4 * PMM_PAGE_SIZE));

    uint64_t again = pmm_allocate_page();
    assert(again);
    assert(pmm_frame_shared(again) == 0);
    for (unsigned i = 0; i < 1000; i++) assert(pmm_frame_ref(again));
    assert(pmm_frame_shared(again) == 1000);
    for (unsigned i = 0; i < 1000; i++) pmm_free_page(again);
    assert(pmm_free_bytes() == initial - PMM_PAGE_SIZE);
    pmm_free_page(again);
    assert(pmm_free_bytes() == initial);
    free(pool);
    puts("PMM: frame reference counts keep shared pages alive until the last owner frees them");
    return 0;
}
