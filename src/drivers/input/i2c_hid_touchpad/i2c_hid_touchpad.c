/*
 * ELAN/ASUE1411 I2C-HID touchpad module.
 *
 * This file intentionally has no kernel-main dependency: it is built as a
 * relocatable module. A future module loader supplies the unresolved kernel
 * symbols and calls purec_i2c_hid_touchpad_init().
 */
#include "arch/x86_64/mmio.h"
#include "drivers/pci/pci.h"
#include "kernel/diagnostics/klog.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#define INTEL_VENDOR_ID 0x8086
#define ALDERLAKE_I2C_0 0x51E8
#define ALDERLAKE_I2C_1 0x51E9
#define DW_I2C_MMIO_SIZE 0x1000U

struct touchpad_module_state {
    struct pci_device_info controller[2];
    uint32_t controller_count;
    bool ready;
};
static struct touchpad_module_state state;

static void find_controller(const struct pci_device_info *pci, void *ctx) {
    (void)ctx;
    if (state.controller_count == 2 || pci->vendor_id != INTEL_VENDOR_ID ||
        (pci->device_id != ALDERLAKE_I2C_0 && pci->device_id != ALDERLAKE_I2C_1))
        return;
    state.controller[state.controller_count++] = *pci;
}

/* Module ABI entry point. The loader must resolve PCI/MMIO/klog symbols. */
bool purec_i2c_hid_touchpad_init(void) {
    state.controller_count = 0;
    state.ready = false;
    pci_enumerate(find_controller, NULL);
    for (uint32_t i = 0; i < state.controller_count; i++) {
        const struct pci_device_info *pci = &state.controller[i];
        uint64_t bar = pci_read_bar(pci->bus, pci->slot, pci->function, 0);
        if (!(bar & 1) && pci_update_command(pci, PCI_COMMAND_MEMORY, 0) &&
            mmio_map(bar & ~0xFULL, DW_I2C_MMIO_SIZE))
            state.ready = true;
    }
    klogf(state.ready ? KLOG_INFO : KLOG_WARN,
          "i2c-hid: ELAN/ASUE1411 module probe: %u Intel DesignWare controller(s)",
          state.controller_count);
    return state.ready;
}
