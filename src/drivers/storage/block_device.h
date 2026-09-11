#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "storage_types.h"

#define BLOCK_SECTOR_SIZE 512

bool block_device_init(void);
uint32_t block_device_rescan_usb(void);
void block_device_poll_usb_hotplug(void);
void block_device_begin_exclusive_io(void);
void block_device_end_exclusive_io(void);
uint32_t block_device_count(void);
int32_t block_device_list(struct storage_device_info *devices, uint32_t capacity);
bool block_device_get_info(uint32_t index, struct storage_device_info *info);
int32_t block_device_find(const char *name);
bool block_device_select(uint32_t index);
bool block_device_read(uint32_t lba, void *buffer);
bool block_device_write(uint32_t lba, const void *buffer);
bool block_device_flush(void);
const char *block_device_name(void);
