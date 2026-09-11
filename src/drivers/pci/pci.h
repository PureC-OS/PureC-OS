#pragma once

#include <stdint.h>
#include <stdbool.h>

#define PCI_COMMAND_IO_SPACE   (1U << 0)
#define PCI_COMMAND_MEMORY     (1U << 1)
#define PCI_COMMAND_BUS_MASTER (1U << 2)

struct pci_device_info {
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t programming_interface;
    uint8_t revision;
};

typedef void (*pci_device_visitor)(const struct pci_device_info *device, void *context);

uint32_t pci_read_config32(uint8_t bus, uint8_t slot, uint8_t function,
                           uint8_t offset);
void pci_write_config32(uint8_t bus, uint8_t slot, uint8_t function,
                        uint8_t offset, uint32_t value);
bool pci_update_command(const struct pci_device_info *device,
                        uint16_t set_bits, uint16_t clear_bits);
uint64_t pci_read_bar(uint8_t bus, uint8_t slot, uint8_t function,
                      uint8_t bar_index);
void pci_enumerate(pci_device_visitor visitor, void *context);
