#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "storage_types.h"

bool ata_pio_init(void);
uint32_t ata_pio_device_count(void);
bool ata_pio_get_device_info(uint32_t index, struct storage_device_info *info);
bool ata_pio_select_device(uint32_t index);
bool ata_pio_read_sector(uint32_t lba, void *buffer);
bool ata_pio_write_sector(uint32_t lba, const void *buffer);
bool ata_pio_flush(void);
const char *ata_pio_device_name(void);
