#pragma once

#include <stdint.h>
#include <stdbool.h>

int32_t ext2_format_device_impl(const char *device_name, const char *serial_confirmation, const char *erase_confirmation);
int32_t ext2_format_at(uint32_t part_lba, uint32_t part_sectors);
bool ext2_format_at_with_progress(uint32_t part_lba, uint32_t part_sectors);
