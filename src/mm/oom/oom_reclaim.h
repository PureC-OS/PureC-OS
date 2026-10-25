#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef bool (*oom_shrinker_fn)(uint64_t want_pages, void *ctx);
int oom_reclaim_register(oom_shrinker_fn fn, void *ctx);
void oom_reclaim_unregister(int id);
uint64_t oom_reclaim_try(uint64_t want_pages);
uint32_t oom_reclaim_count(void);
