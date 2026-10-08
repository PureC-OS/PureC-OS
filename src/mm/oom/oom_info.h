#pragma once
#include <stdint.h>
struct oom_info {
    uint64_t total_pages;
    uint64_t free_pages;
    uint64_t reserve_pages;
    uint64_t budget_pages;
    uint64_t proc_limit_pages;
    uint64_t total_user_pages;
    uint32_t tracked_processes;
    uint32_t pressure;
    uint64_t slab_pages;
    uint32_t shrinkers;
};
void oom_info_get(struct oom_info *out);
