#pragma once

#include <stdbool.h>
#include <stdint.h>

#define IO_LOCK_CHUNK_SECTORS 8U
#define IO_BULK_BURST_MS 3U
#define IO_BULK_PAUSE_MS 1U

struct io_stats
{
    uint64_t sectors_read;
    uint64_t sectors_written;
    uint64_t bulk_sectors;
    uint64_t bulk_pauses;
    uint64_t lock_contended;
    uint32_t bulk_active;
};

void io_bulk_begin(void);
void io_bulk_end(void);
bool io_bulk_active(void);
void io_account(bool write, uint32_t sectors);
void io_throttle(uint32_t sectors);
bool io_dev_lock(void);
void io_dev_unlock(bool preempt_held);
void io_stats_get(struct io_stats *out);
