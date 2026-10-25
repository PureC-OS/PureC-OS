#include "oom.h"
#include "../pmm.h"
#define OOM_PAGE_SIZE 4096ULL
#define OOM_RESERVE_MIN_PAGES (8ULL * 1024ULL * 1024ULL / OOM_PAGE_SIZE)
#define OOM_RESERVE_MAX_PAGES (64ULL * 1024ULL * 1024ULL / OOM_PAGE_SIZE)
#define OOM_PROC_CAP_PAGES (512ULL * 1024ULL * 1024ULL / OOM_PAGE_SIZE)
static bool oom_ready;
static uint64_t oom_reserve_pages;
static uint64_t oom_proc_limit_pages;
void oom_init(void)
{
    uint64_t total_pages = pmm_total_bytes() / OOM_PAGE_SIZE;
    uint64_t reserve = total_pages / 32;
    if (reserve < OOM_RESERVE_MIN_PAGES) {
        reserve = OOM_RESERVE_MIN_PAGES;
    }
    if (reserve > OOM_RESERVE_MAX_PAGES) {
        reserve = OOM_RESERVE_MAX_PAGES;
    }
    uint64_t usable = total_pages > reserve ? total_pages - reserve : 0;
    uint64_t per_proc = usable / 2;
    if (per_proc > OOM_PROC_CAP_PAGES) {
        per_proc = OOM_PROC_CAP_PAGES;
    }
    if (per_proc < OOM_RESERVE_MIN_PAGES) {
        per_proc = OOM_RESERVE_MIN_PAGES;
    }
    oom_reserve_pages = reserve;
    oom_proc_limit_pages = per_proc;
    oom_ready = true;
}
bool oom_is_ready(void)
{
    return oom_ready;
}
uint64_t oom_kernel_reserve_pages(void)
{
    return oom_reserve_pages;
}
uint64_t oom_kernel_reserve_bytes(void)
{
    return oom_reserve_pages * OOM_PAGE_SIZE;
}
uint64_t oom_user_process_limit_pages(void)
{
    return oom_proc_limit_pages;
}
uint64_t oom_user_process_limit_bytes(void)
{
    return oom_proc_limit_pages * OOM_PAGE_SIZE;
}
uint64_t oom_user_budget_pages(void)
{
    uint64_t free_pages = pmm_free_bytes() / OOM_PAGE_SIZE;
    if (!oom_ready) {
        return free_pages;
    }
    if (free_pages <= oom_reserve_pages) {
        return 0;
    }
    return free_pages - oom_reserve_pages;
}
bool oom_kernel_allowed(uint64_t page_count)
{
    if (!page_count) {
        return true;
    }
    return pmm_free_bytes() / OOM_PAGE_SIZE >= page_count;
}
bool oom_user_allowed(uint64_t page_count)
{
    if (!page_count) {
        return true;
    }
    if (!oom_ready) {
        return pmm_free_bytes() / OOM_PAGE_SIZE >= page_count;
    }
    uint64_t free_pages = pmm_free_bytes() / OOM_PAGE_SIZE;
    if (page_count > free_pages) {
        return false;
    }
    return free_pages - page_count >= oom_reserve_pages;
}
bool oom_user_process_allowed(uint64_t current_user_pages, uint64_t request_pages)
{
    if (!request_pages) {
        return true;
    }
    if (request_pages > oom_proc_limit_pages) {
        return false;
    }
    if (current_user_pages > oom_proc_limit_pages) {
        return false;
    }
    return current_user_pages + request_pages <= oom_proc_limit_pages;
}
uint64_t oom_alloc_user_page(void)
{
    if (!oom_user_allowed(1)) {
        return 0;
    }
    return pmm_allocate_page();
}
uint64_t oom_alloc_user_pages(uint64_t page_count)
{
    if (!page_count) {
        return 0;
    }
    if (!oom_user_allowed(page_count)) {
        return 0;
    }
    return pmm_allocate_contiguous(page_count);
}
