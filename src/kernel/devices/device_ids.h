#pragma once
#include <stdbool.h>
#include <stdint.h>

#define DEVMAN_ANY_VENDOR 0xFFFFu
#define DEVMAN_ANY_DEVICE 0xFFFFu
#define DEVMAN_ANY_CLASS 0xFFu
#define DEVMAN_ANY_SUBCLASS 0xFFu
#define DEVMAN_ANY_PROGIF 0xFFu

struct devman_pci_id {
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
};

static inline bool devman_pci_id_matches(uint16_t vendor_id,
                                         uint16_t device_id,
                                         uint8_t class_code,
                                         uint8_t subclass,
                                         uint8_t prog_if,
                                         const struct devman_pci_id *id) {
    if (!id) return false;
    if (id->vendor_id != DEVMAN_ANY_VENDOR && id->vendor_id != vendor_id)
        return false;
    if (id->device_id != DEVMAN_ANY_DEVICE && id->device_id != device_id)
        return false;
    if (id->class_code != DEVMAN_ANY_CLASS && id->class_code != class_code)
        return false;
    if (id->subclass != DEVMAN_ANY_SUBCLASS && id->subclass != subclass)
        return false;
    if (id->prog_if != DEVMAN_ANY_PROGIF && id->prog_if != prog_if)
        return false;
    return true;
}

static inline bool devman_pci_match_any(uint16_t vendor_id, uint16_t device_id, uint8_t class_code, uint8_t subclass, uint8_t prog_if, const struct devman_pci_id *ids, uint32_t count) 
{
    if (!ids) return false;
    for (uint32_t i = 0; i < count; i++) {
        if (devman_pci_id_matches(vendor_id, device_id, class_code,
                                  subclass, prog_if, &ids[i]))
            return true;
    }
    return false;
}

static const struct devman_pci_id DEVMAN_ID_E1000_82540EM[] = {
    { 0x8086, 0x100E, DEVMAN_ANY_CLASS, DEVMAN_ANY_SUBCLASS,
      DEVMAN_ANY_PROGIF },
};

static const struct devman_pci_id DEVMAN_ID_E1000_82543GC[] = {
    { 0x8086, 0x1004, DEVMAN_ANY_CLASS, DEVMAN_ANY_SUBCLASS,
      DEVMAN_ANY_PROGIF },
    { 0x8086, 0x1001, DEVMAN_ANY_CLASS, DEVMAN_ANY_SUBCLASS,
      DEVMAN_ANY_PROGIF },
};

static const struct devman_pci_id DEVMAN_ID_PCNET[] = {
    { 0x1022, 0x2000, DEVMAN_ANY_CLASS, DEVMAN_ANY_SUBCLASS,
      DEVMAN_ANY_PROGIF },
    { 0x1022, 0x2001, DEVMAN_ANY_CLASS, DEVMAN_ANY_SUBCLASS,
      DEVMAN_ANY_PROGIF },
};

static const struct devman_pci_id DEVMAN_ID_AR9285[] = {
    { 0x168C, 0x002B, DEVMAN_ANY_CLASS, DEVMAN_ANY_SUBCLASS,
      DEVMAN_ANY_PROGIF },
};

static const struct devman_pci_id DEVMAN_ID_AHCI[] = {
    { DEVMAN_ANY_VENDOR, DEVMAN_ANY_DEVICE, 0x01, 0x06, 0x01 },
};

static const struct devman_pci_id DEVMAN_ID_NVME[] = {
    { DEVMAN_ANY_VENDOR, DEVMAN_ANY_DEVICE, 0x01, 0x08,
      DEVMAN_ANY_PROGIF },
};

static const struct devman_pci_id DEVMAN_ID_XHCI[] = {
    { DEVMAN_ANY_VENDOR, DEVMAN_ANY_DEVICE, 0x0C, 0x03, 0x30 },
};

static const struct devman_pci_id DEVMAN_ID_EHCI[] = {
    { DEVMAN_ANY_VENDOR, DEVMAN_ANY_DEVICE, 0x0C, 0x03, 0x20 },
};

static const struct devman_pci_id DEVMAN_ID_HDA[] = {
    { DEVMAN_ANY_VENDOR, DEVMAN_ANY_DEVICE, 0x04, 0x03,
      DEVMAN_ANY_PROGIF },
};
