#include "kernel/devices/device_ids.h"

#include <assert.h>
#include <stdio.h>

#define COUNT(table) (sizeof(table) / sizeof((table)[0]))

static void check_tables(void) {
    assert(devman_pci_match_any(0x8086, 0x100E, 0x02, 0x00, 0x00,
                                DEVMAN_ID_E1000_82540EM,
                                COUNT(DEVMAN_ID_E1000_82540EM)));
    assert(!devman_pci_match_any(0x8086, 0x100F, 0x02, 0x00, 0x00,
                                 DEVMAN_ID_E1000_82540EM,
                                 COUNT(DEVMAN_ID_E1000_82540EM)));
    assert(!devman_pci_match_any(0x1022, 0x100E, 0x02, 0x00, 0x00,
                                 DEVMAN_ID_E1000_82540EM,
                                 COUNT(DEVMAN_ID_E1000_82540EM)));

    assert(devman_pci_match_any(0x8086, 0x1004, 0x02, 0x00, 0x00,
                                DEVMAN_ID_E1000_82543GC,
                                COUNT(DEVMAN_ID_E1000_82543GC)));
    assert(devman_pci_match_any(0x8086, 0x1001, 0x02, 0x00, 0x00,
                                DEVMAN_ID_E1000_82543GC,
                                COUNT(DEVMAN_ID_E1000_82543GC)));
    assert(!devman_pci_match_any(0x8086, 0x100E, 0x02, 0x00, 0x00,
                                 DEVMAN_ID_E1000_82543GC,
                                 COUNT(DEVMAN_ID_E1000_82543GC)));

    assert(devman_pci_match_any(0x1022, 0x2000, 0x02, 0x00, 0x00,
                                DEVMAN_ID_PCNET, COUNT(DEVMAN_ID_PCNET)));
    assert(devman_pci_match_any(0x1022, 0x2001, 0x02, 0x00, 0x00,
                                DEVMAN_ID_PCNET, COUNT(DEVMAN_ID_PCNET)));
    assert(!devman_pci_match_any(0x1022, 0x2002, 0x02, 0x00, 0x00,
                                 DEVMAN_ID_PCNET, COUNT(DEVMAN_ID_PCNET)));
    assert(!devman_pci_match_any(0x8086, 0x100E, 0x02, 0x00, 0x00,
                                 DEVMAN_ID_PCNET, COUNT(DEVMAN_ID_PCNET)));

    assert(devman_pci_match_any(0x168C, 0x002B, 0x02, 0x80, 0x00,
                                DEVMAN_ID_AR9285, COUNT(DEVMAN_ID_AR9285)));
    assert(!devman_pci_match_any(0x168C, 0x002C, 0x02, 0x80, 0x00,
                                 DEVMAN_ID_AR9285, COUNT(DEVMAN_ID_AR9285)));

    assert(devman_pci_match_any(0x8086, 0x2922, 0x01, 0x06, 0x01,
                                DEVMAN_ID_AHCI, COUNT(DEVMAN_ID_AHCI)));
    assert(devman_pci_match_any(0x1022, 0x7801, 0x01, 0x06, 0x01,
                                DEVMAN_ID_AHCI, COUNT(DEVMAN_ID_AHCI)));
    assert(!devman_pci_match_any(0x8086, 0x2922, 0x01, 0x06, 0x00,
                                 DEVMAN_ID_AHCI, COUNT(DEVMAN_ID_AHCI)));
    assert(!devman_pci_match_any(0x8086, 0x2922, 0x01, 0x01, 0x00,
                                 DEVMAN_ID_AHCI, COUNT(DEVMAN_ID_AHCI)));

    assert(devman_pci_match_any(0x144D, 0xA808, 0x01, 0x08, 0x02,
                                DEVMAN_ID_NVME, COUNT(DEVMAN_ID_NVME)));
    assert(!devman_pci_match_any(0x8086, 0x2922, 0x01, 0x06, 0x01,
                                 DEVMAN_ID_NVME, COUNT(DEVMAN_ID_NVME)));
    assert(!devman_pci_match_any(0x144D, 0xA808, 0x01, 0x08, 0x02,
                                 DEVMAN_ID_AHCI, COUNT(DEVMAN_ID_AHCI)));

    assert(devman_pci_match_any(0x8086, 0x1E31, 0x0C, 0x03, 0x30,
                                DEVMAN_ID_XHCI, COUNT(DEVMAN_ID_XHCI)));
    assert(!devman_pci_match_any(0x8086, 0x1E31, 0x0C, 0x03, 0x20,
                                 DEVMAN_ID_XHCI, COUNT(DEVMAN_ID_XHCI)));
    assert(devman_pci_match_any(0x8086, 0x1E26, 0x0C, 0x03, 0x20,
                                DEVMAN_ID_EHCI, COUNT(DEVMAN_ID_EHCI)));
    assert(!devman_pci_match_any(0x8086, 0x1E26, 0x0C, 0x03, 0x30,
                                 DEVMAN_ID_EHCI, COUNT(DEVMAN_ID_EHCI)));
    assert(!devman_pci_match_any(0x8086, 0x1E26, 0x0C, 0x03, 0x00,
                                 DEVMAN_ID_XHCI, COUNT(DEVMAN_ID_XHCI)));
    assert(!devman_pci_match_any(0x8086, 0x1E26, 0x0C, 0x03, 0x00,
                                 DEVMAN_ID_EHCI, COUNT(DEVMAN_ID_EHCI)));

    assert(devman_pci_match_any(0x8086, 0x1E20, 0x04, 0x03, 0x00,
                                DEVMAN_ID_HDA, COUNT(DEVMAN_ID_HDA)));
    assert(!devman_pci_match_any(0x8086, 0x2415, 0x04, 0x01, 0x00,
                                 DEVMAN_ID_HDA, COUNT(DEVMAN_ID_HDA)));
}

static void check_edge_cases(void) {
    struct devman_pci_id any = { DEVMAN_ANY_VENDOR, DEVMAN_ANY_DEVICE,
                                 DEVMAN_ANY_CLASS, DEVMAN_ANY_SUBCLASS,
                                 DEVMAN_ANY_PROGIF };
    assert(devman_pci_id_matches(0x1234, 0x5678, 0xFF, 0xFF, 0xFF, &any));
    assert(!devman_pci_id_matches(0x1234, 0x5678, 0xFF, 0xFF, 0xFF, NULL));
    assert(!devman_pci_match_any(0x8086, 0x100E, 0x02, 0x00, 0x00, NULL,
                                 COUNT(DEVMAN_ID_E1000_82540EM)));
    assert(!devman_pci_match_any(0x8086, 0x100E, 0x02, 0x00, 0x00,
                                 DEVMAN_ID_E1000_82540EM, 0));
}

int main(void) {
    check_tables();
    check_edge_cases();
    printf("devman_match_test: all assertions passed\n");
    return 0;
}
