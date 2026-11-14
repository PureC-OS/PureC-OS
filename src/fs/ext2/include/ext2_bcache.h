#pragma once

#include <stdbool.h>
#include <stdint.h>

#define EXT2_BCACHE_MAX_AGE_MS 3000u

struct ext2_bcache_stats {
  uint32_t capacity;
  uint32_t dirty;
  uint64_t hits;
  uint64_t misses;
  uint64_t checkpoints;
  uint64_t runs;
};

bool ext2_bcache_read(uint32_t block, void *out);
bool ext2_bcache_write(uint32_t block, const void *in, uint32_t count);
bool ext2_bcache_checkpoint(void);
void ext2_bcache_tick(void);
void ext2_bcache_reset(bool write_back);
void ext2_bcache_stats(struct ext2_bcache_stats *out);
void ext2_bcache_bind(const char *device);
