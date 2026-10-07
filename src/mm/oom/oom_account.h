#pragma once
#include <stdbool.h>
#include <stdint.h>
void oom_account_init(void);
bool oom_account_charge(uint32_t pid, uint64_t pages);
void oom_account_uncharge(uint32_t pid, uint64_t pages);
void oom_account_remove(uint32_t pid);
uint64_t oom_account_used(uint32_t pid);
uint64_t oom_account_total_user_pages(void);
uint32_t oom_account_tracked_count(void);
