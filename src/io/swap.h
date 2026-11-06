#pragma once

#include <stdbool.h>
#include <stdint.h>

#define SWAP_PAGE_BYTES 4096U
#define SWAP_PAGE_SECTORS 8U
#define SWAP_MAX_PAGES (512U * 1024U)
#define SWAP_MIN_PARTITION_SECTORS 16384U
#define SWAP_MAGIC "PURESWAP"
#define SWAP_VERSION 1U

struct swap_info
{
    bool enabled;
    uint32_t total_pages;
    uint32_t used_pages;
    uint64_t pages_out;
    uint64_t pages_in;
    uint32_t device_index;
    uint32_t start_lba;
};

extern const uint8_t swap_gpt_type_guid[16];

bool swap_format(uint32_t device_index, uint32_t start_lba, uint32_t sectors);
bool swap_enable(uint32_t device_index, uint32_t start_lba, uint32_t sectors);
bool swap_probe(void);
bool swap_enabled(void);
uint32_t swap_alloc(void);
bool swap_dup(uint32_t slot);
void swap_free(uint32_t slot);
bool swap_write(uint32_t slot, const void *page);
bool swap_read(uint32_t slot, void *page);
void swap_info_get(struct swap_info *out);
void swap_daemon_start(void);
