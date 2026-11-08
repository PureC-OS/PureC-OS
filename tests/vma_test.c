#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct vma_table;
struct vma_table *vma_table_create(void);
void vma_table_destroy(struct vma_table *table);
struct vma_table *vma_table_clone(const struct vma_table *source);
int vma_map(struct vma_table *table, uint64_t start, uint64_t end, uint32_t prot);
int vma_unmap(struct vma_table *table, uint64_t start, uint64_t end);
int vma_set_prot(struct vma_table *table, uint64_t start, uint64_t end, uint32_t prot);
_Bool vma_lookup(struct vma_table *table, uint64_t address, uint32_t *prot, uint64_t *start, uint64_t *end);
_Bool vma_range_free(struct vma_table *table, uint64_t start, uint64_t end);
uint64_t vma_find_gap(struct vma_table *table, uint64_t length, uint64_t low, uint64_t high);
uint32_t vma_count(struct vma_table *table);
int vma_map_shm(struct vma_table *table, uint64_t start, uint64_t end, uint32_t prot, int32_t shm, uint64_t offset_pages);
_Bool vma_lookup_shm(struct vma_table *table, uint64_t address, int32_t *shm, uint64_t *page_offset);
_Bool vma_range_has_shm(struct vma_table *table, uint64_t start, uint64_t end);

static int shm_refs[8];
void shm_ref(int32_t id) { shm_refs[id]++; }
void shm_unref(int32_t id) { shm_refs[id]--; }

void *oom_slab_alloc(size_t size) { return calloc(1, size); }
void oom_slab_free(void *pointer, size_t size) { (void)size; free(pointer); }
uint64_t pmm_allocate_contiguous(uint64_t pages)
{
    void *memory = aligned_alloc(4096, pages * 4096);
    return (uint64_t)(uintptr_t)memory;
}
void pmm_free_contiguous(uint64_t physical, uint64_t pages)
{
    (void)pages;
    free((void *)(uintptr_t)physical);
}
void *pmm_physical_to_virtual(uint64_t physical) { return (void *)(uintptr_t)physical; }

#define PAGES 512
static int model[PAGES];
static int failures;

#define CHECK(condition)                                                  \
    do {                                                                  \
        if (!(condition)) {                                               \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #condition);    \
            failures++;                                                   \
            return;                                                       \
        }                                                                 \
    } while (0)

static void compare(struct vma_table *table)
{
    for (int page = 0; page < PAGES; page++) {
        uint32_t prot = 99;
        uint64_t start, end;
        _Bool found = vma_lookup(table, (uint64_t)page * 4096 + 17, &prot, &start, &end);
        if (model[page] < 0) {
            CHECK(!found);
        } else {
            CHECK(found);
            CHECK((int)prot == model[page]);
            CHECK(start <= (uint64_t)page * 4096 && end > (uint64_t)page * 4096);
            CHECK(start % 4096 == 0 && end % 4096 == 0);
        }
    }
    uint32_t runs = 0;
    for (int page = 0; page < PAGES; page++)
        if (model[page] >= 0 && (page == 0 || model[page - 1] != model[page]))
            runs++;
    CHECK(vma_count(table) == runs);
}

static int covered(int first, int last)
{
    for (int page = first; page < last; page++)
        if (model[page] < 0)
            return 0;
    return 1;
}

static void gap_check(struct vma_table *table, int pages)
{
    uint64_t base = vma_find_gap(table, (uint64_t)pages * 4096, 0, (uint64_t)PAGES * 4096);
    int expected = -1;
    for (int start = 0; start + pages <= PAGES && expected < 0; start++) {
        int free = 1;
        for (int page = start; page < start + pages; page++)
            if (model[page] >= 0) { free = 0; break; }
        if (free) expected = start;
    }
    if (expected < 0) {
        CHECK(base == 0);
    } else {
        CHECK(base == (uint64_t)expected * 4096);
    }
}


#define CHECK_MAIN(condition)                                             \
    do {                                                                  \
        if (!(condition)) {                                               \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #condition);    \
            return 1;                                                     \
        }                                                                 \
    } while (0)


static int shm_check_at(struct vma_table *table, uint64_t page, int32_t want_id, uint64_t want_offset)
{
    int32_t id = -9;
    uint64_t offset = 0;
    if (!vma_lookup_shm(table, page * 4096 + 5, &id, &offset))
        return 0;
    return id == want_id && offset == want_offset;
}

static void shm_scenarios(void)
{
    memset(shm_refs, 0, sizeof(shm_refs));
    shm_refs[3] = 1;
    struct vma_table *t = vma_table_create();
    CHECK(vma_map_shm(t, 0x100000, 0x108000, 3, 3, 10) == 0);
    CHECK(shm_refs[3] == 2);
    CHECK(vma_count(t) == 1);
    CHECK(shm_check_at(t, 0x102, 3, 12));
    CHECK(vma_range_has_shm(t, 0x100000, 0x101000));
    CHECK(!vma_range_has_shm(t, 0x200000, 0x201000));

    CHECK(vma_set_prot(t, 0x102000, 0x104000, 1) == 0);
    CHECK(vma_count(t) == 3);
    CHECK(shm_refs[3] == 4);
    CHECK(shm_check_at(t, 0x100, 3, 10));
    CHECK(shm_check_at(t, 0x102, 3, 12));
    CHECK(shm_check_at(t, 0x103, 3, 13));
    CHECK(shm_check_at(t, 0x105, 3, 15));
    CHECK(shm_check_at(t, 0x107, 3, 17));
    uint32_t prot = 0;
    CHECK(vma_lookup(t, 0x102000, &prot, 0, 0) && prot == 1);
    CHECK(vma_lookup(t, 0x105000, &prot, 0, 0) && prot == 3);

    CHECK(vma_set_prot(t, 0x102000, 0x104000, 3) == 0);
    CHECK(vma_count(t) == 1);
    CHECK(shm_refs[3] == 2);
    CHECK(shm_check_at(t, 0x106, 3, 16));

    CHECK(vma_unmap(t, 0x103000, 0x105000) == 0);
    CHECK(vma_count(t) == 2);
    CHECK(shm_refs[3] == 3);
    CHECK(shm_check_at(t, 0x102, 3, 12));
    CHECK(shm_check_at(t, 0x105, 3, 15));
    CHECK(shm_check_at(t, 0x107, 3, 17));
    CHECK(!vma_lookup_shm(t, 0x103000, &(int32_t){0}, &(uint64_t){0}));

    struct vma_table *copy = vma_table_clone(t);
    CHECK(copy != NULL);
    CHECK(shm_refs[3] == 5);
    CHECK(shm_check_at(copy, 0x106, 3, 16));
    vma_table_destroy(copy);
    CHECK(shm_refs[3] == 3);

    CHECK(vma_map(t, 0x100000, 0x102000, 3) == 0);
    CHECK(shm_refs[3] == 2);
    CHECK(!vma_lookup_shm(t, 0x100000, &(int32_t){0}, &(uint64_t){0}));
    CHECK(shm_check_at(t, 0x102, 3, 12));

    CHECK(vma_map_shm(t, 0x200000, 0x201000, 3, 3, 0) == 0);
    CHECK(vma_map_shm(t, 0x201000, 0x202000, 3, 3, 5) == 0);
    CHECK(vma_lookup(t, 0x200000, &prot, 0, 0));
    uint64_t run_start = 0, run_end = 0;
    CHECK(vma_lookup(t, 0x200000, &prot, &run_start, &run_end) && run_end == 0x201000);
    CHECK(vma_map_shm(t, 0x202000, 0x203000, 3, 3, 6) == 0);
    CHECK(vma_lookup(t, 0x201000, &prot, &run_start, &run_end) && run_end == 0x203000);
    CHECK(shm_check_at(t, 0x202, 3, 6));

    CHECK(vma_set_prot(t, 0x200000, 0x203000, 1) == 0);
    CHECK(shm_check_at(t, 0x200, 3, 0));
    CHECK(shm_check_at(t, 0x201, 3, 5));
    CHECK(shm_check_at(t, 0x202, 3, 6));
    CHECK(vma_lookup(t, 0x202000, &prot, 0, 0) && prot == 1);

    vma_table_destroy(t);
    CHECK(shm_refs[3] == 1);
}

int main(void)
{
    srand(12345);
    struct vma_table *table = vma_table_create();
    for (int page = 0; page < PAGES; page++)
        model[page] = -1;
    for (int step = 0; step < 200000; step++) {
        int first = rand() % PAGES;
        int length = 1 + rand() % 40;
        int last = first + length > PAGES ? PAGES : first + length;
        int operation = rand() % 10;
        if (operation < 4) {
            int prot = rand() % 4;
            CHECK_MAIN(vma_map(table, (uint64_t)first * 4096, (uint64_t)last * 4096, (uint32_t)prot) == 0);
            for (int page = first; page < last; page++) model[page] = prot;
        } else if (operation < 6) {
            CHECK_MAIN(vma_unmap(table, (uint64_t)first * 4096, (uint64_t)last * 4096) == 0);
            for (int page = first; page < last; page++) model[page] = -1;
        } else if (operation < 9) {
            int prot = rand() % 4;
            int rc = vma_set_prot(table, (uint64_t)first * 4096, (uint64_t)last * 4096, (uint32_t)prot);
            if (covered(first, last)) {
                CHECK_MAIN(rc == 0);
                for (int page = first; page < last; page++) model[page] = prot;
            } else {
                CHECK_MAIN(rc != 0);
            }
        } else {
            int free = 1;
            for (int page = first; page < last; page++)
                if (model[page] >= 0) { free = 0; break; }
            CHECK_MAIN(vma_range_free(table, (uint64_t)first * 4096, (uint64_t)last * 4096) == free);
            gap_check(table, length);
        }
        if (step % 50 == 0 || step < 200) {
            compare(table);
            if (failures) break;
        }
    }
    compare(table);
    shm_scenarios();
    struct vma_table *copy = vma_table_clone(table);
    CHECK_MAIN(copy != NULL);
    compare(copy);
    vma_map(copy, 0, 4096, 3);
    uint32_t prot = 0;
    vma_lookup(table, 0, &prot, 0, 0);
    CHECK_MAIN(model[0] < 0 || (int)prot == model[0]);
    vma_table_destroy(copy);
    vma_table_destroy(table);
    if (failures) {
        printf("vma_test: %d failures\n", failures);
        return 1;
    }
    printf("vma_test: ok\n");
    return 0;
}
