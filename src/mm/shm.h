#pragma once

#include <stdbool.h>
#include <stdint.h>

#define SHM_NAME_MAX 64
#define SHM_MAX_BYTES (1ULL << 30)

int32_t shm_create(uint64_t size);
void shm_ref(int32_t id);
void shm_unref(int32_t id);
int32_t shm_open_named(const char *name, bool create, bool exclusive,
                       int32_t *id);
int32_t shm_unlink_named(const char *name);
int32_t shm_truncate(int32_t id, uint64_t size);
uint64_t shm_size(int32_t id);
uint64_t shm_page(int32_t id, uint64_t page_index);
