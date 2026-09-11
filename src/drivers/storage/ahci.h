#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "storage_types.h"

struct ahci_probe_stats {
    uint32_t controllers;
    uint32_t implemented_ports;
    uint32_t sata_ports;
    uint32_t identify_failures;
};

void ahci_set_address_mapping(uint64_t hhdm_offset, uint64_t kernel_physical_base,
                              uint64_t kernel_virtual_base);
bool ahci_init(uint32_t linux_name_base);
uint32_t ahci_device_count(void);
bool ahci_get_device_info(uint32_t index, struct storage_device_info *info);
bool ahci_select_device(uint32_t index);
bool ahci_read_sector(uint32_t lba, void *buffer);
bool ahci_write_sector(uint32_t lba, const void *buffer);
bool ahci_flush(void);
const char *ahci_device_name(void);
void ahci_get_probe_stats(struct ahci_probe_stats *stats);
