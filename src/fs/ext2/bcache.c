#include "include/ext2_bcache.h"
#include "include/ext2_types.h"
#include "include/ext2_block.h"
#include "../../drivers/interrupts/timer.h"
#include "../../io/storage/block_device.h"
#include "../../kernel/diagnostics/klog.h"
#include "../../kernel/sync/mutex.h"
#include "../../lib/string.h"
#include "../../mm/pmm.h"

#define BCACHE_POOL_BYTES (16u * 1024u * 1024u)
#define BCACHE_CHUNK_PAGES 256u
#define BCACHE_CHUNK_BYTES (BCACHE_CHUNK_PAGES * 4096u)
#define BCACHE_MAX_ENTRIES (BCACHE_POOL_BYTES / 1024u)
#define BCACHE_CHUNKS (BCACHE_POOL_BYTES / BCACHE_CHUNK_BYTES)
#define BCACHE_HASH 32768u
#define BCACHE_RUN_BYTES (512u * 1024u)
#define BCACHE_NONE (-1)

struct bentry {
  uint32_t block;
  int32_t hash_next;
  int32_t lru_prev;
  int32_t lru_next;
  bool used;
  bool dirty;
};

static mutex_t bcache_mutex;
static struct bentry entries[BCACHE_MAX_ENTRIES];
static int32_t hash_heads[BCACHE_HASH];
static int32_t sort_a[BCACHE_MAX_ENTRIES];
static int32_t sort_b[BCACHE_MAX_ENTRIES];
static uint8_t *chunks[BCACHE_CHUNKS];
static uint8_t *run_buffer;
static uint32_t block_size;
static uint32_t capacity;
static uint32_t per_chunk;
static uint32_t dirty_count;
static int32_t lru_head = BCACHE_NONE;
static int32_t lru_tail = BCACHE_NONE;
static int32_t free_list = BCACHE_NONE;
static uint64_t first_dirty_ms;
static bool ready;
static bool disabled;
static uint64_t stat_hits, stat_misses, stat_checkpoints, stat_runs;
static char bound_device[32];

void ext2_bcache_bind(const char *device) {
  MUTEX_SCOPE(&bcache_mutex);
  uint32_t n = 0;
  while (device && device[n] && n + 1 < sizeof(bound_device)) {
    bound_device[n] = device[n];
    n++;
  }
  bound_device[n] = '\0';
}

static bool device_matches(void) {
  return !bound_device[0] || strcmp(block_device_name(), bound_device) == 0;
}

static uint8_t *entry_data(int32_t index) {
  return chunks[(uint32_t)index / per_chunk] +
         ((uint32_t)index % per_chunk) * block_size;
}

static uint32_t hash_of(uint32_t block) {
  return (block * 2654435761u) & (BCACHE_HASH - 1);
}

static void lru_unlink(int32_t i) {
  struct bentry *e = &entries[i];
  if (e->lru_prev != BCACHE_NONE)
    entries[e->lru_prev].lru_next = e->lru_next;
  else
    lru_head = e->lru_next;
  if (e->lru_next != BCACHE_NONE)
    entries[e->lru_next].lru_prev = e->lru_prev;
  else
    lru_tail = e->lru_prev;
  e->lru_prev = e->lru_next = BCACHE_NONE;
}

static void lru_push_front(int32_t i) {
  struct bentry *e = &entries[i];
  e->lru_prev = BCACHE_NONE;
  e->lru_next = lru_head;
  if (lru_head != BCACHE_NONE)
    entries[lru_head].lru_prev = i;
  lru_head = i;
  if (lru_tail == BCACHE_NONE)
    lru_tail = i;
}

static void hash_remove(int32_t i) {
  uint32_t h = hash_of(entries[i].block);
  int32_t *link = &hash_heads[h];
  while (*link != BCACHE_NONE) {
    if (*link == i) {
      *link = entries[i].hash_next;
      entries[i].hash_next = BCACHE_NONE;
      return;
    }
    link = &entries[*link].hash_next;
  }
}

static int32_t lookup(uint32_t block) {
  for (int32_t i = hash_heads[hash_of(block)]; i != BCACHE_NONE;
       i = entries[i].hash_next)
    if (entries[i].block == block)
      return i;
  return BCACHE_NONE;
}

static void reset_tables(void) {
  for (uint32_t i = 0; i < BCACHE_HASH; i++)
    hash_heads[i] = BCACHE_NONE;
  lru_head = lru_tail = BCACHE_NONE;
  free_list = BCACHE_NONE;
  for (int32_t i = (int32_t)capacity - 1; i >= 0; i--) {
    entries[i].used = false;
    entries[i].dirty = false;
    entries[i].hash_next = free_list;
    entries[i].lru_prev = entries[i].lru_next = BCACHE_NONE;
    free_list = i;
  }
  dirty_count = 0;
}

static bool setup(void) {
  if (ready)
    return true;
  if (disabled)
    return false;
  block_size = ext2_volume()->block_size;
  if (block_size < 1024 || block_size > 4096)
    return false;
  if (!chunks[0]) {
    for (uint32_t c = 0; c < BCACHE_CHUNKS; c++) {
      uint64_t phys = pmm_allocate_contiguous(BCACHE_CHUNK_PAGES);
      if (!phys)
        break;
      chunks[c] = (uint8_t *)pmm_physical_to_virtual(phys);
    }
    uint64_t run_phys = pmm_allocate_contiguous(BCACHE_RUN_BYTES / 4096u);
    if (!chunks[0] || !run_phys) {
      klog(KLOG_WARN, "ext2: buffer cache disabled, no memory");
      disabled = true;
      return false;
    }
    run_buffer = (uint8_t *)pmm_physical_to_virtual(run_phys);
  }
  uint32_t chunk_count = 0;
  while (chunk_count < BCACHE_CHUNKS && chunks[chunk_count])
    chunk_count++;
  per_chunk = BCACHE_CHUNK_BYTES / block_size;
  capacity = chunk_count * per_chunk;
  if (capacity > BCACHE_MAX_ENTRIES)
    capacity = BCACHE_MAX_ENTRIES;
  reset_tables();
  ready = true;
  klogf(KLOG_OK, "ext2: buffer cache %u blocks of %u bytes (%u KiB)", capacity,
        block_size, (capacity * block_size) / 1024u);
  return true;
}

static bool disk_io(bool write, uint32_t block, void *buffer, uint32_t count) {
  uint32_t spb = block_size / BLOCK_SECTOR_SIZE;
  uint32_t lba = ext2_volume()->partition_lba + block * spb;
  return write ? block_device_write_sectors(lba, buffer, count * spb)
               : block_device_read_sectors(lba, buffer, count * spb);
}

static void merge_sort(uint32_t n) {
  int32_t *src = sort_a;
  int32_t *dst = sort_b;
  for (uint32_t width = 1; width < n; width *= 2) {
    for (uint32_t lo = 0; lo < n; lo += 2 * width) {
      uint32_t mid = lo + width < n ? lo + width : n;
      uint32_t hi = lo + 2 * width < n ? lo + 2 * width : n;
      uint32_t a = lo, b = mid, k = lo;
      while (a < mid && b < hi)
        dst[k++] = entries[src[a]].block <= entries[src[b]].block ? src[a++]
                                                                  : src[b++];
      while (a < mid)
        dst[k++] = src[a++];
      while (b < hi)
        dst[k++] = src[b++];
    }
    int32_t *t = src;
    src = dst;
    dst = t;
  }
  if (src != sort_a)
    memcpy(sort_a, src, n * sizeof(int32_t));
}

static bool checkpoint_body(void);

static bool checkpoint_locked(void) {
  if (!ready || !dirty_count)
    return true;
  if (device_matches())
    return checkpoint_body();
  return false;
}

static bool checkpoint_forced(void) {
  if (!ready || !dirty_count)
    return true;
  if (device_matches())
    return checkpoint_body();
  int32_t previous = block_device_find(block_device_name());
  int32_t bound = block_device_find(bound_device);
  if (bound < 0 || !block_device_select((uint32_t)bound)) {
    klogf(KLOG_ERROR, "ext2: cannot reach %s, %u dirty blocks lost", bound_device,
          dirty_count);
    return false;
  }
  bool ok = checkpoint_body();
  if (previous >= 0)
    (void)block_device_select((uint32_t)previous);
  return ok;
}

static bool checkpoint_body(void) {
  uint32_t n = 0;
  for (uint32_t i = 0; i < capacity; i++)
    if (entries[i].used && entries[i].dirty)
      sort_a[n++] = (int32_t)i;
  merge_sort(n);
  uint32_t max_run = BCACHE_RUN_BYTES / block_size;
  bool ok = true;
  uint32_t i = 0;
  while (i < n) {
    uint32_t start = entries[sort_a[i]].block;
    uint32_t run = 1;
    while (i + run < n && run < max_run &&
           entries[sort_a[i + run]].block == start + run)
      run++;
    if (run == 1) {
      ok &= disk_io(true, start, entry_data(sort_a[i]), 1);
    } else {
      for (uint32_t k = 0; k < run; k++)
        memcpy(run_buffer + k * block_size, entry_data(sort_a[i + k]),
               block_size);
      ok &= disk_io(true, start, run_buffer, run);
    }
    for (uint32_t k = 0; k < run; k++)
      entries[sort_a[i + k]].dirty = false;
    stat_runs++;
    i += run;
  }
  dirty_count = 0;
  first_dirty_ms = 0;
  stat_checkpoints++;
  (void)block_device_flush();
  if (!ok)
    klog(KLOG_ERROR, "ext2: checkpoint write failed");
  return ok;
}

static int32_t take_entry(void) {
  if (free_list != BCACHE_NONE) {
    int32_t i = free_list;
    free_list = entries[i].hash_next;
    entries[i].hash_next = BCACHE_NONE;
    return i;
  }
  int32_t victim = lru_tail;
  while (victim != BCACHE_NONE && entries[victim].dirty)
    victim = entries[victim].lru_prev;
  if (victim == BCACHE_NONE) {
    checkpoint_forced();
    victim = lru_tail;
    while (victim != BCACHE_NONE && entries[victim].dirty)
      victim = entries[victim].lru_prev;
  }
  if (victim == BCACHE_NONE)
    return BCACHE_NONE;
  hash_remove(victim);
  lru_unlink(victim);
  entries[victim].used = false;
  return victim;
}

static int32_t insert(uint32_t block) {
  int32_t i = take_entry();
  if (i == BCACHE_NONE)
    return BCACHE_NONE;
  struct bentry *e = &entries[i];
  e->block = block;
  e->used = true;
  e->dirty = false;
  uint32_t h = hash_of(block);
  e->hash_next = hash_heads[h];
  hash_heads[h] = i;
  lru_push_front(i);
  return i;
}

static void touch(int32_t i) {
  if (lru_head == i)
    return;
  lru_unlink(i);
  lru_push_front(i);
}

static void mark_dirty(int32_t i) {
  if (!entries[i].dirty) {
    entries[i].dirty = true;
    dirty_count++;
    if (!first_dirty_ms)
      first_dirty_ms = timer_ticks();
  }
  if (dirty_count >= capacity / 4 * 3)
    checkpoint_locked();
}

bool ext2_bcache_read(uint32_t block, void *out) {
  MUTEX_SCOPE(&bcache_mutex);
  if (!setup())
    return disk_io(false, block, out, 1);
  int32_t i = lookup(block);
  if (i != BCACHE_NONE) {
    stat_hits++;
    touch(i);
    memcpy(out, entry_data(i), block_size);
    return true;
  }
  stat_misses++;
  i = insert(block);
  if (i == BCACHE_NONE)
    return disk_io(false, block, out, 1);
  if (!disk_io(false, block, entry_data(i), 1)) {
    hash_remove(i);
    lru_unlink(i);
    entries[i].used = false;
    entries[i].hash_next = free_list;
    free_list = i;
    return false;
  }
  memcpy(out, entry_data(i), block_size);
  return true;
}

bool ext2_bcache_write(uint32_t block, const void *in, uint32_t count) {
  MUTEX_SCOPE(&bcache_mutex);
  if (!setup())
    return disk_io(true, block, (void *)in, count);
  const uint8_t *bytes = (const uint8_t *)in;
  for (uint32_t k = 0; k < count; k++) {
    int32_t i = lookup(block + k);
    if (i == BCACHE_NONE)
      i = insert(block + k);
    else
      touch(i);
    if (i == BCACHE_NONE) {
      if (!disk_io(true, block + k, (void *)(bytes + k * block_size), 1))
        return false;
      continue;
    }
    memcpy(entry_data(i), bytes + k * block_size, block_size);
    mark_dirty(i);
  }
  return true;
}

bool ext2_bcache_checkpoint(void) {
  MUTEX_SCOPE(&bcache_mutex);
  return checkpoint_forced();
}

void ext2_bcache_tick(void) {
  MUTEX_SCOPE(&bcache_mutex);
  if (!ready || !dirty_count)
    return;
  uint64_t age = timer_ticks() - first_dirty_ms;
  if (age >= EXT2_BCACHE_MAX_AGE_MS || dirty_count >= capacity / 4)
    checkpoint_locked();
}

void ext2_bcache_reset(bool write_back) {
  MUTEX_SCOPE(&bcache_mutex);
  if (!ready)
    return;
  if (write_back)
    checkpoint_forced();
  reset_tables();
  ready = false;
}

void ext2_bcache_stats(struct ext2_bcache_stats *out) {
  MUTEX_SCOPE(&bcache_mutex);
  out->capacity = capacity;
  out->dirty = dirty_count;
  out->hits = stat_hits;
  out->misses = stat_misses;
  out->checkpoints = stat_checkpoints;
  out->runs = stat_runs;
}
