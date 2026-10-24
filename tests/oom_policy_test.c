#include "mm/oom/oom.h"
#include "mm/oom/oom_account.h"
#include "mm/pmm.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define TEST_PAGE 4096ULL
#define TEST_POOL_PAGES 262144
static uint8_t test_pool[TEST_POOL_PAGES * 4096];
static uint64_t test_free_pages;
static uint64_t test_bump;
static uint64_t test_base_phys = 0x100000ULL;
uint64_t pmm_total_bytes(void){ return (uint64_t)TEST_POOL_PAGES * TEST_PAGE; }
uint64_t pmm_free_bytes(void){ return test_free_pages * TEST_PAGE; }
uint64_t pmm_allocate_page(void){
    if(!test_free_pages) return 0;
    uint64_t phys = test_base_phys + test_bump * TEST_PAGE;
    test_bump++;
    test_free_pages--;
    memset(test_pool + (test_bump - 1) * TEST_PAGE, 0, TEST_PAGE);
    return phys;
}
uint64_t pmm_allocate_contiguous(uint64_t count){
    if(!count || count > test_free_pages) return 0;
    uint64_t phys = test_base_phys + test_bump * TEST_PAGE;
    test_bump += count;
    test_free_pages -= count;
    return phys;
}
void pmm_free_page(uint64_t phys){ (void)phys; test_free_pages++; }
void pmm_free_contiguous(uint64_t phys, uint64_t count){ (void)phys; test_free_pages += count; }
void *pmm_physical_to_virtual(uint64_t phys){
    uint64_t off = (phys - test_base_phys) / TEST_PAGE;
    if(phys < test_base_phys || off >= TEST_POOL_PAGES) return NULL;
    return test_pool + off * TEST_PAGE;
}
bool pmm_is_ready(void){ return true; }
static void test_reserve_math(void){
    test_free_pages = TEST_POOL_PAGES;
    test_bump = 0;
    oom_init();
    assert(oom_is_ready());
    assert(oom_kernel_reserve_pages() == 8192);
    assert(oom_kernel_reserve_bytes() == 8192ULL * TEST_PAGE);
    assert(oom_user_process_limit_pages() == 126976);
    assert(oom_user_budget_pages() == (uint64_t)TEST_POOL_PAGES - 8192);
}
static void test_user_gate(void){
    test_free_pages = TEST_POOL_PAGES;
    test_bump = 0;
    oom_init();
    uint64_t budget = oom_user_budget_pages();
    assert(oom_user_allowed(budget));
    assert(!oom_user_allowed(budget + 1));
    assert(oom_user_allowed(0));
    test_free_pages = 8192;
    assert(oom_user_budget_pages() == 0);
    assert(!oom_user_allowed(1));
    assert(oom_kernel_allowed(1));
    assert(oom_kernel_allowed(8192));
    assert(!oom_kernel_allowed(8193));
    test_free_pages = 0;
    assert(!oom_user_allowed(1));
    assert(!oom_kernel_allowed(1));
    assert(oom_kernel_allowed(0));
}
static void test_per_process_cap(void){
    test_free_pages = TEST_POOL_PAGES;
    test_bump = 0;
    oom_init();
    oom_account_init();
    uint64_t lim = oom_user_process_limit_pages();
    assert(oom_user_process_allowed(0, lim));
    assert(!oom_user_process_allowed(0, lim + 1));
    assert(!oom_user_process_allowed(lim, 1));
    assert(oom_user_process_allowed(lim - 10, 10));
    assert(oom_account_charge(42, 100));
    assert(oom_account_used(42) == 100);
    assert(oom_account_total_user_pages() == 100);
    assert(!oom_account_charge(42, lim));
    oom_account_uncharge(42, 40);
    assert(oom_account_used(42) == 60);
    oom_account_remove(42);
    assert(oom_account_used(42) == 0);
    assert(oom_account_total_user_pages() == 0);
    assert(!oom_account_charge(0, 10));
    assert(!oom_account_charge(7, 0));
}
static void test_alloc_gate(void){
    test_free_pages = TEST_POOL_PAGES;
    test_bump = 0;
    oom_init();
    assert(oom_alloc_user_page() != 0);
    assert(oom_alloc_user_pages(16) != 0);
    assert(oom_alloc_user_pages(0) == 0);
    test_free_pages = 8192;
    assert(oom_alloc_user_page() == 0);
    assert(oom_alloc_user_pages(4) == 0);
}
int main(void){
    test_reserve_math();
    test_user_gate();
    test_per_process_cap();
    test_alloc_gate();
    puts("OOM reserve gates, per-process caps, accounting and alloc denials passed");
    return 0;
}
