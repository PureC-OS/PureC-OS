#include "swap.h"
#include "storage/block_device.h"
#include "../kernel/diagnostics/klog.h"
#include "../kernel/process/scheduler.h"
#include "../kernel/sync/spinlock.h"
#include "../lib/string.h"
#include "../mm/oom/oom.h"
#include "../mm/pmm.h"
#include "../mm/vmm.h"

#define SWAP_DAEMON_PERIOD_MS 200U
#define SWAP_RECLAIM_BATCH 32U

const uint8_t swap_gpt_type_guid[16] = {
    0x6D, 0xFD, 0x57, 0x06, 0xAB, 0xA4, 0xC4, 0x43,
    0x84, 0xE5, 0x09, 0x33, 0xC8, 0x4B, 0x4F, 0x4F};

static spinlock_t swap_lock = SPINLOCK_INIT;
static bool swap_on;
static uint32_t swap_device;
static uint32_t swap_start;
static uint32_t swap_pages;
static uint32_t swap_used;
static uint32_t swap_cursor;
static uint8_t *swap_refs;
static uint64_t swap_refs_phys;
static uint64_t swap_refs_pages;
static uint64_t swap_out_count;
static uint64_t swap_in_count;

static uint32_t read_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void write_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

bool swap_format(uint32_t device_index, uint32_t start_lba, uint32_t sectors)
{
    if (sectors < SWAP_MIN_PARTITION_SECTORS)
        return false;
    uint32_t pages = sectors / SWAP_PAGE_SECTORS;
    if (pages > SWAP_MAX_PAGES)
        pages = SWAP_MAX_PAGES;
    uint8_t page[SWAP_PAGE_BYTES];
    memset(page, 0, sizeof(page));
    memcpy(page, SWAP_MAGIC, 8);
    write_u32(page + 8, SWAP_VERSION);
    write_u32(page + 12, pages);
    write_u32(page + 16, SWAP_PAGE_BYTES);
    if (!block_device_raw_run(device_index, true, start_lba, page, SWAP_PAGE_SECTORS))
        return false;
    klogf(KLOG_OK, "swap: formatted %u pages (%u MiB) at lba %u",
          pages, pages / 256U, start_lba);
    return true;
}

bool swap_enable(uint32_t device_index, uint32_t start_lba, uint32_t sectors)
{
    if (swap_on)
        return true;
    uint8_t page[SWAP_PAGE_BYTES];
    if (!block_device_raw_run(device_index, false, start_lba, page, SWAP_PAGE_SECTORS))
        return false;
    if (memcmp(page, SWAP_MAGIC, 8) != 0 || read_u32(page + 8) != SWAP_VERSION ||
        read_u32(page + 16) != SWAP_PAGE_BYTES)
        return false;
    uint32_t pages = read_u32(page + 12);
    if (pages < 2 || pages > SWAP_MAX_PAGES || pages > sectors / SWAP_PAGE_SECTORS)
        return false;
    uint64_t ref_pages = ((uint64_t)pages + PMM_PAGE_SIZE - 1) / PMM_PAGE_SIZE;
    uint64_t phys = pmm_allocate_contiguous(ref_pages);
    if (!phys)
    {
        klog(KLOG_WARN, "swap: no memory for slot table");
        return false;
    }
    uint64_t flags = spin_lock_irqsave(&swap_lock);
    swap_refs = (uint8_t *)pmm_physical_to_virtual(phys);
    swap_refs_phys = phys;
    swap_refs_pages = ref_pages;
    swap_device = device_index;
    swap_start = start_lba;
    swap_pages = pages;
    swap_used = 0;
    swap_cursor = 1;
    swap_on = true;
    spin_unlock_irqrestore(&swap_lock, flags);
    klogf(KLOG_OK, "swap: enabled %u pages (%u MiB) on device %u lba %u",
          pages - 1, (pages - 1) / 256U, device_index, start_lba);
    return true;
}

static bool probe_device(uint32_t index)
{
    uint8_t sector[BLOCK_SECTOR_SIZE];
    if (!block_device_raw_run(index, false, 1, sector, 1) || memcmp(sector, "EFI PART", 8) != 0)
        return false;
    uint64_t entries_lba = 0;
    for (int b = 0; b < 8; b++)
        entries_lba |= (uint64_t)sector[72 + b] << (b * 8);
    uint32_t count = read_u32(sector + 80);
    uint32_t size = read_u32(sector + 84);
    if (!count || size != 128 || entries_lba > UINT32_MAX)
        return false;
    if (count > 128)
        count = 128;
    uint32_t per = BLOCK_SECTOR_SIZE / 128;
    for (uint32_t i = 0; i < count; i++)
    {
        if (i % per == 0 && !block_device_raw_run(index, false, (uint32_t)entries_lba + i / per, sector, 1))
            return false;
        const uint8_t *entry = sector + (i % per) * 128;
        if (memcmp(entry, swap_gpt_type_guid, 16) != 0)
            continue;
        uint64_t first = 0, last = 0;
        for (int b = 0; b < 8; b++)
        {
            first |= (uint64_t)entry[32 + b] << (b * 8);
            last |= (uint64_t)entry[40 + b] << (b * 8);
        }
        if (first > UINT32_MAX || last < first || last - first > UINT32_MAX)
            continue;
        if (swap_enable(index, (uint32_t)first, (uint32_t)(last - first + 1)))
            return true;
        return false;
    }
    return false;
}

bool swap_probe(void)
{
    if (swap_on)
        return true;
    uint32_t count = block_device_count();
    for (uint32_t index = 0; index < count; index++)
    {
        struct storage_device_info info;
        if (!block_device_get_info(index, &info))
            continue;
        if (info.transport != STORAGE_TRANSPORT_ATA_PIO && info.transport != STORAGE_TRANSPORT_AHCI)
            continue;
        if (probe_device(index))
            return true;
    }
    return false;
}

bool swap_enabled(void)
{
    return __atomic_load_n(&swap_on, __ATOMIC_ACQUIRE);
}

uint32_t swap_alloc(void)
{
    if (!swap_enabled())
        return 0;
    uint64_t flags = spin_lock_irqsave(&swap_lock);
    uint32_t result = 0;
    if (swap_used + 1 < swap_pages)
    {
        for (uint32_t scanned = 0; scanned < swap_pages; scanned++)
        {
            if (swap_cursor >= swap_pages || swap_cursor == 0)
                swap_cursor = 1;
            uint32_t slot = swap_cursor++;
            if (!swap_refs[slot])
            {
                swap_refs[slot] = 1;
                swap_used++;
                result = slot;
                break;
            }
        }
    }
    spin_unlock_irqrestore(&swap_lock, flags);
    return result;
}

bool swap_dup(uint32_t slot)
{
    if (!swap_enabled() || slot == 0 || slot >= swap_pages)
        return false;
    uint64_t flags = spin_lock_irqsave(&swap_lock);
    bool ok = swap_refs[slot] != 0 && swap_refs[slot] < 255;
    if (ok)
        swap_refs[slot]++;
    spin_unlock_irqrestore(&swap_lock, flags);
    return ok;
}

void swap_free(uint32_t slot)
{
    if (!swap_enabled() || slot == 0 || slot >= swap_pages)
        return;
    uint64_t flags = spin_lock_irqsave(&swap_lock);
    if (swap_refs[slot] && swap_refs[slot] != 255 && --swap_refs[slot] == 0)
        swap_used--;
    spin_unlock_irqrestore(&swap_lock, flags);
}

static bool slot_valid(uint32_t slot)
{
    return swap_enabled() && slot != 0 && slot < swap_pages;
}

bool swap_write(uint32_t slot, const void *page)
{
    if (!slot_valid(slot) || !page)
        return false;
    bool ok = block_device_raw_run(swap_device, true, swap_start + slot * SWAP_PAGE_SECTORS,
                                   (void *)page, SWAP_PAGE_SECTORS);
    if (ok)
        __atomic_add_fetch(&swap_out_count, 1, __ATOMIC_RELAXED);
    return ok;
}

bool swap_read(uint32_t slot, void *page)
{
    if (!slot_valid(slot) || !page)
        return false;
    bool ok = block_device_raw_run(swap_device, false, swap_start + slot * SWAP_PAGE_SECTORS,
                                   page, SWAP_PAGE_SECTORS);
    if (ok)
        __atomic_add_fetch(&swap_in_count, 1, __ATOMIC_RELAXED);
    return ok;
}

void swap_info_get(struct swap_info *out)
{
    if (!out)
        return;
    memset(out, 0, sizeof(*out));
    if (!swap_enabled())
        return;
    uint64_t flags = spin_lock_irqsave(&swap_lock);
    out->enabled = true;
    out->total_pages = swap_pages - 1;
    out->used_pages = swap_used;
    out->device_index = swap_device;
    out->start_lba = swap_start;
    spin_unlock_irqrestore(&swap_lock, flags);
    out->pages_out = __atomic_load_n(&swap_out_count, __ATOMIC_RELAXED);
    out->pages_in = __atomic_load_n(&swap_in_count, __ATOMIC_RELAXED);
}

static void swap_daemon(void *argument)
{
    (void)argument;
    if (!swap_probe())
    {
        klog(KLOG_INFO, "swap: no swap partition found, daemon exits");
        scheduler_exit();
        return;
    }
    uint64_t reserve = oom_kernel_reserve_pages();
    uint64_t low = reserve * 4;
    uint64_t high = reserve * 8;
    if (low < 2048)
        low = 2048;
    if (high < low * 2)
        high = low * 2;
    for (;;)
    {
        scheduler_sleep(SWAP_DAEMON_PERIOD_MS);
        uint64_t free_pages = pmm_free_bytes() / PMM_PAGE_SIZE;
        if (free_pages >= low)
            continue;
        while (free_pages < high)
        {
            uint32_t done = vmm_swap_reclaim(SWAP_RECLAIM_BATCH);
            if (!done)
                break;
            scheduler_sleep(1);
            free_pages = pmm_free_bytes() / PMM_PAGE_SIZE;
        }
    }
}

void swap_daemon_start(void)
{
    int tid = scheduler_create_thread(swap_daemon, 0, "kswapd", 3, -1);
    if (tid < 0)
    {
        klog(KLOG_WARN, "swap: failed to start kswapd");
        return;
    }
    scheduler_set_qos(tid, SCHED_QOS_BACKGROUND);
}
