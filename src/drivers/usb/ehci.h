#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "../storage/storage_types.h"

struct ehci_probe_stats {
    uint32_t controllers;
    uint32_t connected_ports;
    uint32_t high_speed_ports;
    uint32_t mass_storage_devices;
    uint32_t failures;
    uint32_t last_stage;
};

void ehci_set_address_mapping(uint64_t hhdm_offset, uint64_t kernel_physical_base,
                              uint64_t kernel_virtual_base);
bool ehci_init(uint32_t linux_name_base);
bool ehci_rescan(uint32_t linux_name_base);
bool ehci_topology_changed(void);
uint32_t ehci_device_count(void);
bool ehci_get_device_info(uint32_t index, struct storage_device_info *info);
bool ehci_select_device(uint32_t index);
bool ehci_read_sector(uint32_t lba, void *buffer);
bool ehci_write_sector(uint32_t lba, const void *buffer);
const char *ehci_device_name(void);
void ehci_get_probe_stats(struct ehci_probe_stats *stats);
