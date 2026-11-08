#include "vma.h"
#include "../../lib/string.h"
#include "../../mm/oom/oom_slab.h"
#include "../../mm/pmm.h"
#include "../sync/spinlock.h"
#include "../syscall/syscall.h"

struct vma_entry
{
  uint64_t start;
  uint64_t end;
  uint32_t prot;
  uint32_t reserved;
};

struct vma_table
{
  spinlock_t lock;
  struct vma_entry *entries;
  uint64_t entries_phys;
  uint32_t entries_pages;
  uint32_t count;
  uint32_t capacity;
};

#define VMA_MIN_CAPACITY 170U
#define VMA_SPLICE_MAX 3U

struct vma_table *vma_table_create(void)
{
  struct vma_table *table =
      (struct vma_table *)oom_slab_alloc(sizeof(struct vma_table));
  if (!table)
    return NULL;
  memset(table, 0, sizeof(*table));
  return table;
}

void vma_table_destroy(struct vma_table *table)
{
  if (!table)
    return;
  if (table->entries_phys)
    pmm_free_contiguous(table->entries_phys, table->entries_pages);
  oom_slab_free(table, sizeof(struct vma_table));
}

static bool vma_reserve_locked(struct vma_table *table, uint32_t extra)
{
  if (table->count + extra <= table->capacity)
    return true;
  uint32_t wanted = table->capacity ? table->capacity * 2 : VMA_MIN_CAPACITY;
  while (wanted < table->count + extra)
    wanted *= 2;
  uint32_t pages = (uint32_t)(((uint64_t)wanted * sizeof(struct vma_entry) +
                               PMM_PAGE_SIZE - 1) /
                              PMM_PAGE_SIZE);
  uint64_t phys = pmm_allocate_contiguous(pages);
  if (!phys)
    return false;
  struct vma_entry *fresh = (struct vma_entry *)pmm_physical_to_virtual(phys);
  if (table->count)
    memcpy(fresh, table->entries, table->count * sizeof(struct vma_entry));
  if (table->entries_phys)
    pmm_free_contiguous(table->entries_phys, table->entries_pages);
  table->entries = fresh;
  table->entries_phys = phys;
  table->entries_pages = pages;
  table->capacity =
      (uint32_t)((uint64_t)pages * PMM_PAGE_SIZE / sizeof(struct vma_entry));
  return true;
}

struct vma_table *vma_table_clone(const struct vma_table *source)
{
  if (!source)
    return NULL;
  struct vma_table *table = vma_table_create();
  if (!table)
    return NULL;
  struct vma_table *locked = (struct vma_table *)source;
  uint64_t flags = spin_lock_irqsave(&locked->lock);
  bool ok = true;
  if (source->count)
  {
    uint32_t pages = source->entries_pages;
    uint64_t phys = pmm_allocate_contiguous(pages);
    if (!phys)
    {
      ok = false;
    }
    else
    {
      table->entries = (struct vma_entry *)pmm_physical_to_virtual(phys);
      table->entries_phys = phys;
      table->entries_pages = pages;
      table->capacity = source->capacity;
      table->count = source->count;
      memcpy(table->entries, source->entries,
             source->count * sizeof(struct vma_entry));
    }
  }
  spin_unlock_irqrestore(&locked->lock, flags);
  if (!ok)
  {
    vma_table_destroy(table);
    return NULL;
  }
  return table;
}

static uint32_t vma_first_ending_after(const struct vma_table *table,
                                       uint64_t address)
{
  uint32_t low = 0;
  uint32_t high = table->count;
  while (low < high)
  {
    uint32_t middle = low + (high - low) / 2;
    if (table->entries[middle].end > address)
      high = middle;
    else
      low = middle + 1;
  }
  return low;
}

static void vma_splice_locked(struct vma_table *table, uint64_t start,
                              uint64_t end, const struct vma_entry *inserted)
{
  uint32_t low = vma_first_ending_after(table, start);
  uint32_t high = low;
  while (high < table->count && table->entries[high].start < end)
    high++;
  struct vma_entry pieces[VMA_SPLICE_MAX];
  uint32_t kept = 0;
  uint32_t inserted_index = UINT32_MAX;
  if (high > low && table->entries[low].start < start)
  {
    pieces[kept] = table->entries[low];
    pieces[kept].end = start;
    kept++;
  }
  if (inserted)
  {
    inserted_index = kept;
    pieces[kept++] = *inserted;
  }
  if (high > low && table->entries[high - 1].end > end)
  {
    pieces[kept] = table->entries[high - 1];
    pieces[kept].start = end;
    kept++;
  }
  uint32_t removed = high - low;
  if (kept != removed)
    memmove(&table->entries[low + kept], &table->entries[high],
            (table->count - high) * sizeof(struct vma_entry));
  for (uint32_t i = 0; i < kept; i++)
    table->entries[low + i] = pieces[i];
  table->count = table->count - removed + kept;
  if (inserted_index == UINT32_MAX)
    return;
  uint32_t at = low + inserted_index;
  if (at + 1 < table->count &&
      table->entries[at].end == table->entries[at + 1].start &&
      table->entries[at].prot == table->entries[at + 1].prot)
  {
    table->entries[at].end = table->entries[at + 1].end;
    memmove(&table->entries[at + 1], &table->entries[at + 2],
            (table->count - at - 2) * sizeof(struct vma_entry));
    table->count--;
  }
  if (at > 0 && table->entries[at - 1].end == table->entries[at].start &&
      table->entries[at - 1].prot == table->entries[at].prot)
  {
    table->entries[at - 1].end = table->entries[at].end;
    memmove(&table->entries[at], &table->entries[at + 1],
            (table->count - at - 1) * sizeof(struct vma_entry));
    table->count--;
  }
}

int vma_map(struct vma_table *table, uint64_t start, uint64_t end,
            uint32_t prot)
{
  if (!table || start >= end)
    return -KERR_INVAL;
  uint64_t flags = spin_lock_irqsave(&table->lock);
  if (!vma_reserve_locked(table, VMA_SPLICE_MAX))
  {
    spin_unlock_irqrestore(&table->lock, flags);
    return -KERR_NOMEM;
  }
  struct vma_entry entry = {.start = start, .end = end, .prot = prot};
  vma_splice_locked(table, start, end, &entry);
  spin_unlock_irqrestore(&table->lock, flags);
  return 0;
}

int vma_unmap(struct vma_table *table, uint64_t start, uint64_t end)
{
  if (!table || start >= end)
    return 0;
  uint64_t flags = spin_lock_irqsave(&table->lock);
  if (!vma_reserve_locked(table, VMA_SPLICE_MAX))
  {
    spin_unlock_irqrestore(&table->lock, flags);
    return -KERR_NOMEM;
  }
  vma_splice_locked(table, start, end, NULL);
  spin_unlock_irqrestore(&table->lock, flags);
  return 0;
}

static bool vma_covered_locked(const struct vma_table *table, uint64_t start,
                               uint64_t end)
{
  uint64_t cursor = start;
  uint32_t index = vma_first_ending_after(table, start);
  while (cursor < end)
  {
    if (index >= table->count || table->entries[index].start > cursor)
      return false;
    cursor = table->entries[index].end;
    index++;
  }
  return true;
}

int vma_set_prot(struct vma_table *table, uint64_t start, uint64_t end,
                 uint32_t prot)
{
  if (!table || start >= end)
    return -KERR_INVAL;
  uint64_t flags = spin_lock_irqsave(&table->lock);
  if (!vma_covered_locked(table, start, end))
  {
    spin_unlock_irqrestore(&table->lock, flags);
    return -KERR_NOMEM;
  }
  if (!vma_reserve_locked(table, VMA_SPLICE_MAX))
  {
    spin_unlock_irqrestore(&table->lock, flags);
    return -KERR_NOMEM;
  }
  struct vma_entry entry = {.start = start, .end = end, .prot = prot};
  vma_splice_locked(table, start, end, &entry);
  spin_unlock_irqrestore(&table->lock, flags);
  return 0;
}

bool vma_lookup(struct vma_table *table, uint64_t address, uint32_t *prot,
                uint64_t *start, uint64_t *end)
{
  if (!table)
    return false;
  uint64_t flags = spin_lock_irqsave(&table->lock);
  uint32_t index = vma_first_ending_after(table, address);
  bool found = index < table->count && table->entries[index].start <= address;
  if (found)
  {
    if (prot)
      *prot = table->entries[index].prot;
    if (start)
      *start = table->entries[index].start;
    if (end)
      *end = table->entries[index].end;
  }
  spin_unlock_irqrestore(&table->lock, flags);
  return found;
}

bool vma_range_free(struct vma_table *table, uint64_t start, uint64_t end)
{
  if (!table)
    return true;
  uint64_t flags = spin_lock_irqsave(&table->lock);
  uint32_t index = vma_first_ending_after(table, start);
  bool free = index >= table->count || table->entries[index].start >= end;
  spin_unlock_irqrestore(&table->lock, flags);
  return free;
}

uint64_t vma_find_gap(struct vma_table *table, uint64_t length, uint64_t low,
                      uint64_t high)
{
  if (!length || low >= high || length > high - low)
    return 0;
  uint64_t flags = 0;
  if (table)
    flags = spin_lock_irqsave(&table->lock);
  uint64_t candidate = low;
  uint64_t result = 0;
  uint32_t index = table ? vma_first_ending_after(table, low) : 0;
  while (table && index < table->count)
  {
    const struct vma_entry *entry = &table->entries[index];
    if (entry->start > candidate && entry->start - candidate >= length)
    {
      result = candidate;
      break;
    }
    if (entry->end > candidate)
      candidate = entry->end;
    if (candidate >= high)
      break;
    index++;
  }
  if (!result && candidate < high && high - candidate >= length)
    result = candidate;
  if (table)
    spin_unlock_irqrestore(&table->lock, flags);
  return result;
}

uint32_t vma_count(struct vma_table *table)
{
  if (!table)
    return 0;
  uint64_t flags = spin_lock_irqsave(&table->lock);
  uint32_t count = table->count;
  spin_unlock_irqrestore(&table->lock, flags);
  return count;
}
