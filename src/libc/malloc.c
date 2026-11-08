// PureC hosted libc: heap allocator.
// Boundary-tag allocator over pc_heap_grow arenas: O(1) coalescing of
// neighbours, segregated free lists (exact bins below 1 KiB, four bins per
// power of two above) and a bitmap of non-empty bins. Arenas never shrink.
// Single-threaded processes only.

#include "include/hosted/stdlib.h"
#include "include/hosted/string.h"
#include "include/hosted/errno.h"
#include "include/purec.h"
#include <stddef.h>
#include <stdint.h>

#define HDR 16
#define MIN_CHUNK 32
#define F_INUSE 1
#define F_PREV 2
#define FLAGS 15
#define ARENA_MIN (256u * 1024u)
#define NBINS 256
#define SMALL_LIMIT 1024
#define MAX_REQUEST ((size_t)1 << 46)

struct chunk {
    size_t prev_size;
    size_t size;
    struct chunk *fd;
    struct chunk *bk;
};

static struct chunk *bins[NBINS];
static uint64_t bin_map[NBINS / 64];

static inline size_t chunk_size(const struct chunk *c) { return c->size & ~(size_t)FLAGS; }

static inline struct chunk *chunk_next(struct chunk *c)
{
    return (struct chunk *)((uint8_t *)c + chunk_size(c));
}

static inline unsigned bin_index(size_t size)
{
    if (size < SMALL_LIMIT)
        return (unsigned)(size >> 4);
    unsigned log = 63u - (unsigned)__builtin_clzl(size);
    unsigned sub = (unsigned)(size >> (log - 2)) & 3u;
    unsigned index = 64u + (log - 10u) * 4u + sub;
    return index >= NBINS ? NBINS - 1 : index;
}

static void bin_insert(struct chunk *c)
{
    unsigned b = bin_index(chunk_size(c));
    c->fd = bins[b];
    c->bk = 0;
    if (bins[b])
        bins[b]->bk = c;
    bins[b] = c;
    bin_map[b >> 6] |= (uint64_t)1 << (b & 63);
}

static void bin_remove(struct chunk *c)
{
    unsigned b = bin_index(chunk_size(c));
    if (c->bk)
        c->bk->fd = c->fd;
    else
        bins[b] = c->fd;
    if (c->fd)
        c->fd->bk = c->bk;
    if (!bins[b])
        bin_map[b >> 6] &= ~((uint64_t)1 << (b & 63));
}

static struct chunk *bin_find(size_t need)
{
    unsigned b = bin_index(need);
    if (b < 64) {
        if (bins[b])
            return bins[b];
    } else {
        for (struct chunk *c = bins[b]; c; c = c->fd)
            if (chunk_size(c) >= need)
                return c;
    }
    unsigned start = b + 1;
    if (start >= NBINS)
        return 0;
    unsigned word = start >> 6;
    uint64_t mask = bin_map[word] & (~(uint64_t)0 << (start & 63));
    while (!mask) {
        if (++word >= NBINS / 64)
            return 0;
        mask = bin_map[word];
    }
    return bins[word * 64 + (unsigned)__builtin_ctzl(mask)];
}

static int heap_extend(size_t need)
{
    size_t request = need + HDR;
    if (request < ARENA_MIN)
        request = ARENA_MIN;
    request = (request + 15) & ~(size_t)15;
    request += 16;
    uint8_t *arena = (uint8_t *)pc_heap_grow(request);
    if (!arena) {
        errno = ENOMEM;
        return 0;
    }
    uint8_t *base = (uint8_t *)(((uintptr_t)arena + 15) & ~(uintptr_t)15);
    size_t usable = (request - (size_t)(base - arena)) & ~(size_t)15;
    size_t span = usable - HDR;
    struct chunk *c = (struct chunk *)base;
    c->prev_size = 0;
    c->size = span | F_PREV;
    struct chunk *sentinel = (struct chunk *)(base + span);
    sentinel->prev_size = span;
    sentinel->size = F_INUSE;
    bin_insert(c);
    return 1;
}

static inline size_t request_to_chunk(size_t size)
{
    size_t need = (size + HDR + 15) & ~(size_t)15;
    return need < MIN_CHUNK ? MIN_CHUNK : need;
}

void *malloc(size_t size)
{
    if (!size)
        size = 1;
    if (size > MAX_REQUEST) {
        errno = ENOMEM;
        return 0;
    }
    size_t need = request_to_chunk(size);
    struct chunk *c = bin_find(need);
    if (!c) {
        if (!heap_extend(need))
            return 0;
        c = bin_find(need);
        if (!c) {
            errno = ENOMEM;
            return 0;
        }
    }
    bin_remove(c);
    size_t total = chunk_size(c);
    size_t remainder = total - need;
    size_t prev_flag = c->size & F_PREV;
    if (remainder >= MIN_CHUNK) {
        c->size = need | prev_flag | F_INUSE;
        struct chunk *rest = (struct chunk *)((uint8_t *)c + need);
        rest->size = remainder | F_PREV;
        struct chunk *after = (struct chunk *)((uint8_t *)rest + remainder);
        after->prev_size = remainder;
        after->size &= ~(size_t)F_PREV;
        bin_insert(rest);
    } else {
        c->size = total | prev_flag | F_INUSE;
        chunk_next(c)->size |= F_PREV;
    }
    return (uint8_t *)c + HDR;
}

void free(void *pointer)
{
    if (!pointer)
        return;
    struct chunk *c = (struct chunk *)((uint8_t *)pointer - HDR);
    size_t size = chunk_size(c);
    size_t prev_flag = c->size & F_PREV;
    struct chunk *after = (struct chunk *)((uint8_t *)c + size);
    if (!prev_flag) {
        struct chunk *before = (struct chunk *)((uint8_t *)c - c->prev_size);
        bin_remove(before);
        size += chunk_size(before);
        prev_flag = before->size & F_PREV;
        c = before;
    }
    if (!(after->size & F_INUSE)) {
        bin_remove(after);
        size += chunk_size(after);
        after = (struct chunk *)((uint8_t *)c + size);
    }
    c->size = size | prev_flag;
    after->prev_size = size;
    after->size &= ~(size_t)F_PREV;
    bin_insert(c);
}

void *calloc(size_t count, size_t size)
{
    if (count && size > (size_t)-1 / count) {
        errno = ENOMEM;
        return 0;
    }
    size_t total = count * size;
    void *pointer = malloc(total);
    if (pointer)
        memset(pointer, 0, total);
    return pointer;
}

void *realloc(void *pointer, size_t size)
{
    if (!pointer)
        return malloc(size);
    if (!size) {
        free(pointer);
        return 0;
    }
    if (size > MAX_REQUEST) {
        errno = ENOMEM;
        return 0;
    }
    struct chunk *c = (struct chunk *)((uint8_t *)pointer - HDR);
    size_t current = chunk_size(c);
    size_t need = request_to_chunk(size);
    size_t prev_flag = c->size & F_PREV;
    struct chunk *after = (struct chunk *)((uint8_t *)c + current);

    if (need <= current) {
        size_t remainder = current - need;
        if (remainder >= MIN_CHUNK) {
            c->size = need | prev_flag | F_INUSE;
            struct chunk *rest = (struct chunk *)((uint8_t *)c + need);
            size_t rest_size = remainder;
            if (!(after->size & F_INUSE)) {
                bin_remove(after);
                rest_size += chunk_size(after);
                after = (struct chunk *)((uint8_t *)c + need + rest_size);
            }
            rest->size = rest_size | F_PREV;
            after->prev_size = rest_size;
            after->size &= ~(size_t)F_PREV;
            bin_insert(rest);
        }
        return pointer;
    }

    if (!(after->size & F_INUSE) && current + chunk_size(after) >= need) {
        size_t total = current + chunk_size(after);
        bin_remove(after);
        struct chunk *beyond = (struct chunk *)((uint8_t *)c + total);
        size_t remainder = total - need;
        if (remainder >= MIN_CHUNK) {
            c->size = need | prev_flag | F_INUSE;
            struct chunk *rest = (struct chunk *)((uint8_t *)c + need);
            rest->size = remainder | F_PREV;
            beyond->prev_size = remainder;
            beyond->size &= ~(size_t)F_PREV;
            bin_insert(rest);
        } else {
            c->size = total | prev_flag | F_INUSE;
            beyond->size |= F_PREV;
        }
        return pointer;
    }

    void *fresh = malloc(size);
    if (!fresh)
        return 0;
    size_t copy = current - HDR;
    memcpy(fresh, pointer, copy < size ? copy : size);
    free(pointer);
    return fresh;
}

void *aligned_alloc(size_t alignment, size_t size)
{
    if (alignment == 0 || (alignment & (alignment - 1))) {
        errno = EINVAL;
        return 0;
    }
    if (!size)
        size = 1;
    if (alignment <= HDR)
        return malloc(size);
    if (size > MAX_REQUEST || alignment > MAX_REQUEST) {
        errno = ENOMEM;
        return 0;
    }
    uint8_t *raw = (uint8_t *)malloc(size + alignment + MIN_CHUNK);
    if (!raw)
        return 0;
    uintptr_t aligned = ((uintptr_t)raw + alignment - 1) & ~(uintptr_t)(alignment - 1);
    if (aligned != (uintptr_t)raw && aligned - (uintptr_t)raw < MIN_CHUNK)
        aligned += alignment;
    uint8_t *pointer = (uint8_t *)aligned;
    if (pointer != raw) {
        struct chunk *head = (struct chunk *)(raw - HDR);
        struct chunk *body = (struct chunk *)(pointer - HDR);
        size_t total = chunk_size(head);
        size_t lead = (size_t)(pointer - raw);
        size_t prev_flag = head->size & F_PREV;
        head->size = lead | prev_flag | F_INUSE;
        body->size = (total - lead) | F_PREV | F_INUSE;
        free(raw);
    }
    return realloc(pointer, size);
}

void *memalign(size_t alignment, size_t size)
{
    return aligned_alloc(alignment, size);
}

int posix_memalign(void **memptr, size_t alignment, size_t size)
{
    if (alignment < sizeof(void *) || (alignment & (alignment - 1)))
        return EINVAL;
    void *pointer = aligned_alloc(alignment, size);
    if (!pointer)
        return ENOMEM;
    *memptr = pointer;
    return 0;
}

size_t malloc_usable_size(void *pointer)
{
    if (!pointer)
        return 0;
    struct chunk *c = (struct chunk *)((uint8_t *)pointer - HDR);
    return chunk_size(c) - HDR;
}
