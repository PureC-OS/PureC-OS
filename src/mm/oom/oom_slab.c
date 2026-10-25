#include "oom_slab.h"
#include "../pmm.h"
#include "../../kernel/sync/spinlock.h"
#define OOM_SLAB_PAGE 4096ULL
#define OOM_SLAB_CLASSES 8
static const uint64_t oom_slab_sizes[OOM_SLAB_CLASSES] = {32, 64, 128, 256, 512, 1024, 2048, 4096};
struct oom_slab_link {
    struct oom_slab_link *next;
};
struct oom_slab_page {
    struct oom_slab_page *next;
    uint64_t phys;
    uint64_t free_count;
    uint64_t total_count;
};
static struct oom_slab_link *oom_free_heads[OOM_SLAB_CLASSES];
static struct oom_slab_page *oom_page_heads[OOM_SLAB_CLASSES];
static uint64_t oom_slab_page_count;
static spinlock_t oom_slab_lock = SPINLOCK_INIT;
static int oom_slab_class(uint64_t size)
{
    for (int i = 0; i < OOM_SLAB_CLASSES; i++) {
        if (size <= oom_slab_sizes[i]) {
            return i;
        }
    }
    return -1;
}
void oom_slab_init(void)
{
    uint64_t flags = spin_lock_irqsave(&oom_slab_lock);
    for (int i = 0; i < OOM_SLAB_CLASSES; i++) {
        oom_free_heads[i] = NULL;
        oom_page_heads[i] = NULL;
    }
    oom_slab_page_count = 0;
    spin_unlock_irqrestore(&oom_slab_lock, flags);
}
static bool oom_slab_grow_locked(int cls)
{
    uint64_t phys = pmm_allocate_page();
    if (!phys) {
        return false;
    }
    uint8_t *base = (uint8_t *)pmm_physical_to_virtual(phys);
    struct oom_slab_page *pg = (struct oom_slab_page *)base;
    uint64_t size = oom_slab_sizes[cls];
    uint64_t off = sizeof(struct oom_slab_page);
    off = (off + 8 - 1) & ~(uint64_t)(8 - 1);
    uint64_t n = 0;
    while (off + size <= OOM_SLAB_PAGE) {
        struct oom_slab_link *link = (struct oom_slab_link *)(base + off);
        link->next = oom_free_heads[cls];
        oom_free_heads[cls] = link;
        off += size;
        n++;
    }
    if (!n) {
        pmm_free_page(phys);
        return false;
    }
    pg->next = oom_page_heads[cls];
    pg->phys = phys;
    pg->free_count = n;
    pg->total_count = n;
    oom_page_heads[cls] = pg;
    oom_slab_page_count++;
    return true;
}
void *oom_slab_alloc(uint64_t size)
{
    if (!size) {
        return NULL;
    }
    int cls = oom_slab_class(size);
    if (cls < 0) {
        return NULL;
    }
    uint64_t flags = spin_lock_irqsave(&oom_slab_lock);
    if (!oom_free_heads[cls] && !oom_slab_grow_locked(cls)) {
        spin_unlock_irqrestore(&oom_slab_lock, flags);
        return NULL;
    }
    struct oom_slab_link *link = oom_free_heads[cls];
    oom_free_heads[cls] = link->next;
    struct oom_slab_page *pg = oom_page_heads[cls];
    while (pg) {
        uint8_t *b = (uint8_t *)pg;
        uint8_t *end = b + OOM_SLAB_PAGE;
        uint8_t *p = (uint8_t *)link;
        if (p >= b && p < end && pg->free_count) {
            pg->free_count--;
            break;
        }
        pg = pg->next;
    }
    spin_unlock_irqrestore(&oom_slab_lock, flags);
    uint8_t *out = (uint8_t *)link;
    for (uint64_t i = 0; i < oom_slab_sizes[cls]; i++) {
        out[i] = 0;
    }
    return out;
}
void oom_slab_free(void *ptr, uint64_t size)
{
    if (!ptr || !size) {
        return;
    }
    int cls = oom_slab_class(size);
    if (cls < 0) {
        return;
    }
    uint64_t flags = spin_lock_irqsave(&oom_slab_lock);
    struct oom_slab_link *link = (struct oom_slab_link *)ptr;
    link->next = oom_free_heads[cls];
    oom_free_heads[cls] = link;
    struct oom_slab_page *pg = oom_page_heads[cls];
    while (pg) {
        uint8_t *b = (uint8_t *)pg;
        uint8_t *p = (uint8_t *)ptr;
        if (p >= b && p < b + OOM_SLAB_PAGE) {
            pg->free_count++;
            break;
        }
        pg = pg->next;
    }
    spin_unlock_irqrestore(&oom_slab_lock, flags);
}
uint64_t oom_slab_pages_used(void)
{
    uint64_t flags = spin_lock_irqsave(&oom_slab_lock);
    uint64_t n = oom_slab_page_count;
    spin_unlock_irqrestore(&oom_slab_lock, flags);
    return n;
}
uint64_t oom_slab_reclaim_empty(void)
{
    uint64_t flags = spin_lock_irqsave(&oom_slab_lock);
    uint64_t freed = 0;
    for (int cls = 0; cls < OOM_SLAB_CLASSES; cls++) {
        struct oom_slab_page *kept_empty = NULL;
        struct oom_slab_page **link = &oom_page_heads[cls];
        while (*link) {
            struct oom_slab_page *pg = *link;
            if (pg->free_count != pg->total_count) {
                link = &pg->next;
                continue;
            }
            if (!kept_empty) {
                kept_empty = pg;
                link = &pg->next;
                continue;
            }
            *link = pg->next;
            uint8_t *pb = (uint8_t *)pg;
            uint8_t *pe = pb + OOM_SLAB_PAGE;
            struct oom_slab_link **flink = &oom_free_heads[cls];
            while (*flink) {
                uint8_t *p = (uint8_t *)(*flink);
                if (p >= pb && p < pe) {
                    *flink = (*flink)->next;
                } else {
                    flink = &(*flink)->next;
                }
            }
            uint64_t phys = pg->phys;
            oom_slab_page_count--;
            freed++;
            pmm_free_page(phys);
        }
    }
    spin_unlock_irqrestore(&oom_slab_lock, flags);
    return freed;
}
