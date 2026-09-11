#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "../storage/storage_types.h"

enum xhci_probe_error {
    XHCI_PROBE_OK=0,
    XHCI_PROBE_MMIO,
    XHCI_PROBE_CAPABILITY,
    XHCI_PROBE_BIOS_HANDOFF,
    XHCI_PROBE_HALT_TIMEOUT,
    XHCI_PROBE_RESET_TIMEOUT,
    XHCI_PROBE_NOT_READY_TIMEOUT,
    XHCI_PROBE_PAGE_SIZE,
    XHCI_PROBE_DMA_ADDRESS,
    XHCI_PROBE_SCRATCHPADS,
    XHCI_PROBE_RUN_TIMEOUT,
    XHCI_PROBE_NO_CONNECTED_PORT,
    XHCI_PROBE_PORT_RESET,
    XHCI_PROBE_EVENT_TIMEOUT,
    XHCI_PROBE_COMPLETION,
    XHCI_PROBE_ENABLE_SLOT,
    XHCI_PROBE_ADDRESS_DEVICE,
    XHCI_PROBE_DEVICE_DESCRIPTOR,
    XHCI_PROBE_CONFIG_DESCRIPTOR,
    XHCI_PROBE_MASS_STORAGE_INTERFACE,
    XHCI_PROBE_CONFIGURE_ENDPOINT,
    XHCI_PROBE_SCSI
};

struct xhci_probe_stats {
    uint32_t controllers;
    uint32_t connected_ports;
    uint32_t addressed_devices;
    uint32_t mass_storage_devices;
    uint32_t hid_mice;
    uint32_t hid_interfaces;
    uint32_t hubs;
    uint32_t mouse_transfer_errors;
    uint32_t failures;
    uint32_t last_stage;
    uint32_t last_error;
    uint32_t last_port;
    uint32_t last_portsc;
    uint32_t last_completion_code;
    uint32_t max_ports;
    uint32_t usb_status;
    uint32_t scratchpad_count;
};

void xhci_set_address_mapping(uint64_t hhdm_offset, uint64_t kernel_physical_base,
                              uint64_t kernel_virtual_base);
bool xhci_init(uint32_t linux_name_base);
bool xhci_rescan(uint32_t linux_name_base);
bool xhci_topology_changed(void);
uint32_t xhci_device_count(void);
bool xhci_get_device_info(uint32_t index, struct storage_device_info *info);
bool xhci_select_device(uint32_t index);
bool xhci_read_sector(uint32_t lba, void *buffer);
bool xhci_write_sector(uint32_t lba, const void *buffer);
const char *xhci_device_name(void);
void xhci_get_probe_stats(struct xhci_probe_stats *stats);
void xhci_poll_mouse(void);
