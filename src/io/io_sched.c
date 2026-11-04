#include "io_sched.h"
#include "../kernel/process/scheduler.h"
#include "../drivers/interrupts/timer.h"

static volatile bool dev_busy;
static volatile int bulk_tid = -1;
static uint8_t bulk_saved_qos;
static uint64_t bulk_slice_start;
static struct io_stats stats;

void io_bulk_begin(void)
{
    if (!scheduler_is_running())
        return;
    int tid = scheduler_current_tid();
    if (tid < 0)
        return;
    bulk_saved_qos = scheduler_get_qos(tid);
    scheduler_set_qos(tid, SCHED_QOS_BACKGROUND);
    bulk_slice_start = timer_ticks();
    __atomic_store_n(&bulk_tid, tid, __ATOMIC_RELEASE);
    __atomic_store_n(&stats.bulk_active, 1, __ATOMIC_RELEASE);
}

void io_bulk_end(void)
{
    int tid = __atomic_load_n(&bulk_tid, __ATOMIC_ACQUIRE);
    if (tid < 0)
        return;
    __atomic_store_n(&bulk_tid, -1, __ATOMIC_RELEASE);
    __atomic_store_n(&stats.bulk_active, 0, __ATOMIC_RELEASE);
    scheduler_set_qos(tid, bulk_saved_qos);
}

bool io_bulk_active(void)
{
    return __atomic_load_n(&bulk_tid, __ATOMIC_ACQUIRE) >= 0;
}

void io_account(bool write, uint32_t sectors)
{
    if (write)
        __atomic_add_fetch(&stats.sectors_written, sectors, __ATOMIC_RELAXED);
    else
        __atomic_add_fetch(&stats.sectors_read, sectors, __ATOMIC_RELAXED);
}

void io_throttle(uint32_t sectors)
{
    int tid = __atomic_load_n(&bulk_tid, __ATOMIC_ACQUIRE);
    if (tid < 0 || !scheduler_is_running() || scheduler_current_tid() != tid)
        return;
    stats.bulk_sectors += sectors;
    uint64_t now = timer_ticks();
    if (now - bulk_slice_start < IO_BULK_BURST_MS)
        return;
    stats.bulk_pauses++;
    scheduler_sleep(IO_BULK_PAUSE_MS);
    bulk_slice_start = timer_ticks();
}

bool io_dev_lock(void)
{
    bool held = scheduler_preempt_disable();
    bool counted = false;
    while (__atomic_test_and_set(&dev_busy, __ATOMIC_ACQUIRE))
    {
        if (!counted)
        {
            __atomic_add_fetch(&stats.lock_contended, 1, __ATOMIC_RELAXED);
            counted = true;
        }
        __asm__ volatile("pause" ::: "memory");
    }
    return held;
}

void io_dev_unlock(bool preempt_held)
{
    __atomic_clear(&dev_busy, __ATOMIC_RELEASE);
    if (preempt_held)
        scheduler_preempt_enable();
}

void io_stats_get(struct io_stats *out)
{
    if (!out)
        return;
    out->sectors_read = __atomic_load_n(&stats.sectors_read, __ATOMIC_RELAXED);
    out->sectors_written = __atomic_load_n(&stats.sectors_written, __ATOMIC_RELAXED);
    out->bulk_sectors = stats.bulk_sectors;
    out->bulk_pauses = stats.bulk_pauses;
    out->lock_contended = __atomic_load_n(&stats.lock_contended, __ATOMIC_RELAXED);
    out->bulk_active = io_bulk_active() ? 1 : 0;
}
