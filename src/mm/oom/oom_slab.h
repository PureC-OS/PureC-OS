#pragma once
#include <stddef.h>
#include <stdint.h>
void oom_slab_init(void);
void *oom_slab_alloc(uint64_t size);
void oom_slab_free(void *ptr, uint64_t size);
uint64_t oom_slab_pages_used(void);
uint64_t oom_slab_reclaim_empty(void);
