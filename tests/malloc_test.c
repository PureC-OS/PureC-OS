/* Host test for src/libc/malloc.c: boundary-tag allocator, including aligned allocation. */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

void *pc_malloc(size_t);
void pc_free(void *);
void *pc_calloc(size_t, size_t);
void *pc_realloc(void *, size_t);
void *pc_aligned_alloc(size_t, size_t);
int pc_posix_memalign(void **, size_t, size_t);
void *pc_memalign(size_t, size_t);
size_t pc_malloc_usable_size(void *);

int pc_errno;
int __pc_threaded;
void __pc_rlock_lock(void *lock) { (void)lock; }
void __pc_rlock_unlock(void *lock) { (void)lock; }
static uint64_t grown_bytes;
static int failures;

void *pc_heap_grow(uint64_t size)
{
    void *memory = mmap(0, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (memory == MAP_FAILED)
        return 0;
    grown_bytes += size;
    return memory;
}

#define CHECK(condition)                                               \
    do                                                                 \
    {                                                                  \
        if (!(condition))                                              \
        {                                                              \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                \
        }                                                              \
    } while (0)

static uint64_t rng_state = 0x9E3779B97F4A7C15ull;
static uint64_t rng(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}

static void fill(uint8_t *block, size_t size, uint8_t seed)
{
    for (size_t i = 0; i < size; i++)
        block[i] = (uint8_t)(seed + i * 7);
}

static int verify(const uint8_t *block, size_t size, uint8_t seed)
{
    for (size_t i = 0; i < size; i++)
        if (block[i] != (uint8_t)(seed + i * 7))
            return 0;
    return 1;
}

static void test_basic(void)
{
    uint8_t *a = pc_malloc(100);
    CHECK(a && ((uintptr_t)a & 15) == 0);
    fill(a, 100, 3);
    a = pc_realloc(a, 5000);
    CHECK(a && verify(a, 100, 3));
    a = pc_realloc(a, 40);
    CHECK(a && verify(a, 40, 3));
    CHECK(pc_malloc_usable_size(a) >= 40);
    pc_free(a);
    uint8_t *z = pc_calloc(64, 8);
    CHECK(z != 0);
    for (int i = 0; i < 512; i++)
        CHECK(z[i] == 0);
    pc_free(z);
    pc_free(0);
}

static void test_aligned(void)
{
    static const size_t alignments[] = {1, 2, 8, 16, 32, 48 / 3 * 2, 64, 128, 256, 4096, 65536};
    static const size_t sizes[] = {1, 7, 16, 31, 100, 1000, 5000, 70000};
    for (size_t a = 0; a < sizeof(alignments) / sizeof(alignments[0]); a++)
    {
        size_t alignment = alignments[a];
        if (alignment & (alignment - 1))
            continue;
        for (size_t s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++)
        {
            uint8_t *block = pc_aligned_alloc(alignment, sizes[s]);
            CHECK(block != 0);
            if (!block)
                continue;
            CHECK(((uintptr_t)block & (alignment - 1)) == 0);
            CHECK(pc_malloc_usable_size(block) >= sizes[s]);
            fill(block, sizes[s], (uint8_t)(a * 13 + s));
            CHECK(verify(block, sizes[s], (uint8_t)(a * 13 + s)));
            block = pc_realloc(block, sizes[s] + 300);
            CHECK(block && verify(block, sizes[s], (uint8_t)(a * 13 + s)));
            pc_free(block);
        }
    }
    CHECK(pc_aligned_alloc(3, 10) == 0 && pc_errno == EINVAL);
    CHECK(pc_aligned_alloc(0, 10) == 0 && pc_errno == EINVAL);
    void *p = 0;
    CHECK(pc_posix_memalign(&p, 4, 10) == EINVAL);
    CHECK(pc_posix_memalign(&p, 24, 10) == EINVAL);
    CHECK(pc_posix_memalign(&p, 64, 10) == 0 && p && ((uintptr_t)p & 63) == 0);
    pc_free(p);
    p = pc_memalign(512, 100);
    CHECK(p && ((uintptr_t)p & 511) == 0);
    pc_free(p);
}

static void test_aligned_reuses_space(void)
{
    uint64_t before = grown_bytes;
    for (int round = 0; round < 2000; round++)
    {
        void *block = pc_aligned_alloc(256, 1000);
        CHECK(block != 0);
        pc_free(block);
    }
    CHECK(grown_bytes - before <= 512 * 1024);
}

#define LIVE 2048
static void test_stress(void)
{
    struct
    {
        uint8_t *block;
        size_t size;
        uint8_t seed;
    } live[LIVE];
    memset(live, 0, sizeof(live));
    for (int step = 0; step < 300000; step++)
    {
        unsigned slot = (unsigned)(rng() % LIVE);
        if (live[slot].block)
        {
            CHECK(verify(live[slot].block, live[slot].size, live[slot].seed));
            if (rng() % 3 == 0)
            {
                size_t size = 1 + (size_t)(rng() % 3000);
                uint8_t *grown = pc_realloc(live[slot].block, size);
                CHECK(grown != 0);
                size_t kept = size < live[slot].size ? size : live[slot].size;
                CHECK(verify(grown, kept, live[slot].seed));
                live[slot].block = grown;
                live[slot].size = size;
                live[slot].seed = (uint8_t)rng();
                fill(grown, size, live[slot].seed);
            }
            else
            {
                pc_free(live[slot].block);
                live[slot].block = 0;
            }
        }
        else
        {
            size_t size = 1 + (size_t)(rng() % (rng() % 50 == 0 ? 40000 : 600));
            uint8_t *block;
            if (rng() % 4 == 0)
            {
                size_t alignment = (size_t)16 << (rng() % 9);
                block = pc_aligned_alloc(alignment, size);
                CHECK(block && ((uintptr_t)block & (alignment - 1)) == 0);
            }
            else
            {
                block = pc_malloc(size);
            }
            CHECK(block != 0);
            if (!block)
                return;
            live[slot].block = block;
            live[slot].size = size;
            live[slot].seed = (uint8_t)rng();
            fill(block, size, live[slot].seed);
        }
    }
    for (int i = 0; i < LIVE; i++)
    {
        if (live[i].block)
        {
            CHECK(verify(live[i].block, live[i].size, live[i].seed));
            pc_free(live[i].block);
        }
    }
    uint64_t before = grown_bytes;
    void *big = pc_malloc(8u * 1024 * 1024);
    CHECK(big != 0);
    pc_free(big);
    (void)before;
}

int main(void)
{
    test_basic();
    test_aligned();
    test_aligned_reuses_space();
    test_stress();
    if (failures)
    {
        printf("malloc_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("malloc_test: ok\n");
    return 0;
}
