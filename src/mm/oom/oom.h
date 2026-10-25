#pragma once
#include <stdbool.h>
#include <stdint.h>
void oom_init(void);
bool oom_is_ready(void);
uint64_t oom_kernel_reserve_bytes(void);
uint64_t oom_kernel_reserve_pages(void);
uint64_t oom_user_budget_pages(void);
uint64_t oom_user_process_limit_pages(void);
uint64_t oom_user_process_limit_bytes(void);
bool oom_user_allowed(uint64_t page_count);
bool oom_kernel_allowed(uint64_t page_count);
bool oom_user_process_allowed(uint64_t current_user_pages, uint64_t request_pages);
uint64_t oom_alloc_user_page(void);
uint64_t oom_alloc_user_pages(uint64_t page_count);
