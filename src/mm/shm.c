#include "shm.h"
#include "../kernel/sync/spinlock.h"
#include "../kernel/syscall/syscall.h"
#include "../lib/string.h"
#include "pmm.h"

#define SHM_MAX 128
#define SHM_LEAF_ENTRIES 512U
#define SHM_ROOT_ENTRIES 512U

struct shm
{
  bool used;
  bool named;
  uint32_t refs;
  uint64_t size;
  char name[SHM_NAME_MAX];
  uint64_t leaves[SHM_ROOT_ENTRIES];
};

static spinlock_t shm_lock;
static struct shm objects[SHM_MAX];

static struct shm *object_get(int32_t id)
{
  if (id < 0 || id >= SHM_MAX || !objects[id].used)
    return 0;
  return &objects[id];
}

static uint64_t page_count_of(uint64_t size)
{
  return (size + PMM_PAGE_SIZE - 1) / PMM_PAGE_SIZE;
}

static void pages_free_from(struct shm *object, uint64_t first_page)
{
  for (uint32_t root = 0; root < SHM_ROOT_ENTRIES; root++)
  {
    uint64_t leaf_phys = object->leaves[root];
    if (!leaf_phys)
      continue;
    uint64_t base = (uint64_t)root * SHM_LEAF_ENTRIES;
    if (base + SHM_LEAF_ENTRIES <= first_page)
      continue;
    uint64_t *leaf = (uint64_t *)pmm_physical_to_virtual(leaf_phys);
    bool empty = true;
    for (uint32_t i = 0; i < SHM_LEAF_ENTRIES; i++)
    {
      if (base + i >= first_page && leaf[i])
      {
        pmm_free_page(leaf[i]);
        leaf[i] = 0;
      }
      if (leaf[i])
        empty = false;
    }
    if (empty)
    {
      pmm_free_page(leaf_phys);
      object->leaves[root] = 0;
    }
  }
}

int32_t shm_create(uint64_t size)
{
  if (size > SHM_MAX_BYTES)
    return -KERR_INVAL;
  uint64_t flags = spin_lock_irqsave(&shm_lock);
  for (int32_t i = 0; i < SHM_MAX; i++)
  {
    if (objects[i].used)
      continue;
    memset(&objects[i], 0, sizeof(objects[i]));
    objects[i].used = true;
    objects[i].refs = 1;
    objects[i].size = size;
    spin_unlock_irqrestore(&shm_lock, flags);
    return i;
  }
  spin_unlock_irqrestore(&shm_lock, flags);
  return -KERR_NFILE;
}

void shm_ref(int32_t id)
{
  uint64_t flags = spin_lock_irqsave(&shm_lock);
  struct shm *object = object_get(id);
  if (object)
    object->refs++;
  spin_unlock_irqrestore(&shm_lock, flags);
}

void shm_unref(int32_t id)
{
  uint64_t flags = spin_lock_irqsave(&shm_lock);
  struct shm *object = object_get(id);
  if (object && object->refs && --object->refs == 0)
  {
    pages_free_from(object, 0);
    memset(object, 0, sizeof(*object));
  }
  spin_unlock_irqrestore(&shm_lock, flags);
}

static bool name_valid(const char *name)
{
  uint32_t length = 0;
  while (name[length])
  {
    if (++length >= SHM_NAME_MAX)
      return false;
  }
  return length > 0;
}

static const char *name_trim(const char *name)
{
  while (*name == '/')
    name++;
  return name;
}

int32_t shm_open_named(const char *name, bool create, bool exclusive,
                       int32_t *id)
{
  name = name_trim(name);
  if (!name_valid(name))
    return -KERR_INVAL;
  for (const char *c = name; *c; c++)
    if (*c == '/')
      return -KERR_INVAL;
  uint64_t flags = spin_lock_irqsave(&shm_lock);
  for (int32_t i = 0; i < SHM_MAX; i++)
  {
    if (objects[i].used && objects[i].named && strcmp(objects[i].name, name) == 0)
    {
      if (create && exclusive)
      {
        spin_unlock_irqrestore(&shm_lock, flags);
        return -KERR_EXIST;
      }
      objects[i].refs++;
      *id = i;
      spin_unlock_irqrestore(&shm_lock, flags);
      return 0;
    }
  }
  if (!create)
  {
    spin_unlock_irqrestore(&shm_lock, flags);
    return -KERR_NOENT;
  }
  for (int32_t i = 0; i < SHM_MAX; i++)
  {
    if (objects[i].used)
      continue;
    memset(&objects[i], 0, sizeof(objects[i]));
    objects[i].used = true;
    objects[i].named = true;
    objects[i].refs = 2;
    strcpy(objects[i].name, name);
    *id = i;
    spin_unlock_irqrestore(&shm_lock, flags);
    return 0;
  }
  spin_unlock_irqrestore(&shm_lock, flags);
  return -KERR_NFILE;
}

int32_t shm_unlink_named(const char *name)
{
  name = name_trim(name);
  if (!name_valid(name))
    return -KERR_INVAL;
  uint64_t flags = spin_lock_irqsave(&shm_lock);
  for (int32_t i = 0; i < SHM_MAX; i++)
  {
    if (!objects[i].used || !objects[i].named || strcmp(objects[i].name, name))
      continue;
    objects[i].named = false;
    objects[i].name[0] = 0;
    spin_unlock_irqrestore(&shm_lock, flags);
    shm_unref(i);
    return 0;
  }
  spin_unlock_irqrestore(&shm_lock, flags);
  return -KERR_NOENT;
}

int32_t shm_truncate(int32_t id, uint64_t size)
{
  if (size > SHM_MAX_BYTES)
    return -KERR_INVAL;
  uint64_t flags = spin_lock_irqsave(&shm_lock);
  struct shm *object = object_get(id);
  if (!object)
  {
    spin_unlock_irqrestore(&shm_lock, flags);
    return -KERR_BADF;
  }
  if (size < object->size)
  {
    pages_free_from(object, page_count_of(size));
    uint64_t tail = size % PMM_PAGE_SIZE;
    if (tail)
    {
      uint64_t index = size / PMM_PAGE_SIZE;
      uint64_t leaf_phys = object->leaves[index / SHM_LEAF_ENTRIES];
      if (leaf_phys)
      {
        uint64_t page = ((uint64_t *)pmm_physical_to_virtual(leaf_phys))
            [index % SHM_LEAF_ENTRIES];
        if (page)
          memset((uint8_t *)pmm_physical_to_virtual(page) + tail, 0,
                 PMM_PAGE_SIZE - tail);
      }
    }
  }
  object->size = size;
  spin_unlock_irqrestore(&shm_lock, flags);
  return 0;
}

uint64_t shm_size(int32_t id)
{
  uint64_t flags = spin_lock_irqsave(&shm_lock);
  struct shm *object = object_get(id);
  uint64_t size = object ? object->size : 0;
  spin_unlock_irqrestore(&shm_lock, flags);
  return size;
}

uint64_t shm_page(int32_t id, uint64_t page_index)
{
  uint64_t flags = spin_lock_irqsave(&shm_lock);
  struct shm *object = object_get(id);
  if (!object || page_index >= page_count_of(object->size) ||
      page_index >= (uint64_t)SHM_ROOT_ENTRIES * SHM_LEAF_ENTRIES)
  {
    spin_unlock_irqrestore(&shm_lock, flags);
    return 0;
  }
  uint32_t root = (uint32_t)(page_index / SHM_LEAF_ENTRIES);
  if (!object->leaves[root])
  {
    uint64_t leaf_phys = pmm_allocate_page();
    if (!leaf_phys)
    {
      spin_unlock_irqrestore(&shm_lock, flags);
      return 0;
    }
    memset(pmm_physical_to_virtual(leaf_phys), 0, PMM_PAGE_SIZE);
    object->leaves[root] = leaf_phys;
  }
  uint64_t *leaf = (uint64_t *)pmm_physical_to_virtual(object->leaves[root]);
  uint32_t slot = (uint32_t)(page_index % SHM_LEAF_ENTRIES);
  if (!leaf[slot])
  {
    uint64_t page = pmm_allocate_page();
    if (!page)
    {
      spin_unlock_irqrestore(&shm_lock, flags);
      return 0;
    }
    memset(pmm_physical_to_virtual(page), 0, PMM_PAGE_SIZE);
    leaf[slot] = page;
  }
  uint64_t result = leaf[slot];
  spin_unlock_irqrestore(&shm_lock, flags);
  return result;
}
