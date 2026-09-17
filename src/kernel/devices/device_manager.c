#include "device_manager.h"
#include "../../drivers/pci/pci.h"
#include "../../acpi/include/acpi/acpi.h"
#include "../diagnostics/klog.h"
#include "../../lib/string.h"
#include <stdint.h>
#include <stdbool.h>

static struct device_info g_devices[DEVICE_MANAGER_MAX_DEVICES];
static uint32_t g_device_count = 0;
static uint32_t g_pci_count = 0;
static uint32_t g_acpi_count = 0;
static bool g_enumerated = false;
static struct device_driver *g_drivers = NULL;

static void device_zero(struct device_info *dev) {
    memset(dev, 0, sizeof(*dev));
    dev->id = 0xFFFFFFFF;
    dev->type = DEVICE_TYPE_UNKNOWN;
}

static void device_set_name(struct device_info *dev, const char *name) {
    memset(dev->name, 0, sizeof(dev->name));
    if (name) {
        size_t i = 0;
        while (name[i] && i < DEVICE_NAME_MAX_LEN - 1) {
            dev->name[i] = name[i];
            i++;
        }
    }
}

static struct device_info *device_add(enum device_type type) {
    if (g_device_count >= DEVICE_MANAGER_MAX_DEVICES)
        return NULL;
    struct device_info *dev = &g_devices[g_device_count];
    device_zero(dev);
    dev->id = g_device_count;
    dev->type = type;
    dev->enabled = true;
    dev->driver_bound = false;
    g_device_count++;
    return dev;
}

static void pci_inspect_device(const struct pci_device_info *pci, void *ctx) {
    (void)ctx;
    if (g_device_count >= DEVICE_MANAGER_MAX_DEVICES)
        return;
    struct device_info *dev = device_add(DEVICE_TYPE_PCI);
    if (!dev) return;
    dev->pci.vendor_id = pci->vendor_id;
    dev->pci.device_id = pci->device_id;
    dev->pci.bus = pci->bus;
    dev->pci.slot = pci->slot;
    dev->pci.function = pci->function;
    dev->pci.class_code = pci->class_code;
    dev->pci.subclass = pci->subclass;
    dev->pci.programming_interface = pci->programming_interface;
    dev->pci.revision = pci->revision;
    dev->pci.interrupt_pin = 0;
    dev->pci.interrupt_line = 0;
    dev->pci.base_addresses[0] = pci_read_bar(pci->bus, pci->slot, pci->function, 0);
    dev->pci.base_addresses[1] = pci_read_bar(pci->bus, pci->slot, pci->function, 1);
    dev->pci.base_addresses[2] = pci_read_bar(pci->bus, pci->slot, pci->function, 2);
    dev->pci.base_addresses[3] = pci_read_bar(pci->bus, pci->slot, pci->function, 3);
    dev->pci.base_addresses[4] = pci_read_bar(pci->bus, pci->slot, pci->function, 4);
    dev->pci.base_addresses[5] = pci_read_bar(pci->bus, pci->slot, pci->function, 5);
    dev->irq_count = 0;
    dev->mmio_base = dev->pci.base_addresses[0];
    dev->mmio_size = 0;
    g_pci_count++;
    device_set_name(dev, "pci-device");
}

static void acpi_visitor_noop(const char *sig, void *table, uint32_t len, void *ctx) { (void)sig; (void)table; (void)len; (void)ctx; }
static void acpi_visitor_pci_bridge(const char *sig, void *table, uint32_t len, void *ctx) { (void)sig; (void)table; (void)len; (void)ctx; }
static void acpi_visitor_sensor(const char *sig, void *table, uint32_t len, void *ctx) { (void)sig; (void)table; (void)len; (void)ctx; }
static void acpi_visitor_network(const char *sig, void *table, uint32_t len, void *ctx) { (void)sig; (void)table; (void)len; (void)ctx; }

static void acpi_visitor_storage(const char *sig, void *table, uint32_t len, void *ctx) { (void)sig; (void)table; (void)len; (void)ctx; }
static void acpi_visitor_usb(const char *sig, void *table, uint32_t len, void *ctx) { (void)sig; (void)table; (void)len; (void)ctx; }
static void acpi_visitor_display(const char *sig, void *table, uint32_t len, void *ctx) { (void)sig; (void)table; (void)len; (void)ctx; }
static void acpi_visitor_audio(const char *sig, void *table, uint32_t len, void *ctx) { (void)sig; (void)table; (void)len; (void)ctx; }

static void acpi_isa_enumerate(void) {
    if (!acpi_is_ready()) return;
    struct acpi_madt_info madt_info;
    if (!acpi_get_madt(&madt_info)) return;
    if (madt_info.ioapic_count == 0) return;
    for (uint32_t i = 0; i < 16 && g_device_count < DEVICE_MANAGER_MAX_DEVICES; i++) {
        struct device_info *dev = device_add(DEVICE_TYPE_IRQ);
        if (!dev) break;
        dev->acpi.address = madt_info.ioapic_first_addr + i * 0x10;
        dev->irq_count = 0;
        device_set_name(dev, "ioapic");
    }
}

static void acpi_madt_devices(void) {
    if (!acpi_is_ready()) return;
    struct acpi_madt_info madt_info;
    if (!acpi_get_madt(&madt_info)) return;
    if (madt_info.lapic_base == 0) return;
    for (uint32_t i = 0; i < madt_info.total_cpus && g_device_count < DEVICE_MANAGER_MAX_DEVICES; i++) {
        struct device_info *dev = device_add(DEVICE_TYPE_IRQ);
        if (!dev) break;
        dev->acpi.address = madt_info.lapic_base;
        dev->irq_count = 1;
        dev->irq_lines[0] = (i == 0) ? 0 : (int)i;
        device_set_name(dev, "lapic");
    }
}

static void acpi_ec_devices(void) {
    if (!acpi_is_ready() || !acpi_has_ec()) return;
    struct device_info *dev = device_add(DEVICE_TYPE_ACPI);
    if (!dev) return;
    const char *hid = "PNP0C09";
    size_t i = 0;
    while (hid[i] && i < ACPI_HID_MAX_LEN - 1) {
        dev->acpi.hid[i] = hid[i];
        i++;
    }
    dev->acpi.hid[i] = '\0';
    dev->irq_count = 1;
    device_set_name(dev, "EC");
    g_acpi_count++;
}

static void acpi_battery_device(void) {
    if (!acpi_is_ready() || !acpi_has_battery()) return;
    struct device_info *dev = device_add(DEVICE_TYPE_ACPI);
    if (!dev) return;
    const char *name = acpi_battery_name();
    if (name && name[0]) {
        size_t i = 0;
        while (name[i] && i < ACPI_HID_MAX_LEN - 1) {
            dev->acpi.hid[i] = name[i];
            i++;
        }
        dev->acpi.hid[i] = '\0';
    } else {
        strncpy(dev->acpi.hid, "BAT0", ACPI_HID_MAX_LEN);
    }
    dev->irq_count = 0;
    device_set_name(dev, "battery");
    g_acpi_count++;
}

static void acpi_gpe_devices(void) {
    if (!acpi_is_ready()) return;
    for (uint8_t i = 0; i < 2 && g_device_count < DEVICE_MANAGER_MAX_DEVICES; i++) {
        struct device_info *dev = device_add(DEVICE_TYPE_IRQ);
        if (!dev) break;
        dev->acpi.address = (i == 0) ? 0x600 : 0x608;
        dev->irq_count = 1;
        dev->irq_lines[0] = 0;
        device_set_name(dev, "gpe");
    }
}

static void acpi_uart_devices(void) {
    if (!acpi_is_ready()) return;
    const char *signatures[] = {"HWS3", "HWS4", "HWS5"};
    for (size_t s = 0; s < 3 && g_device_count < DEVICE_MANAGER_MAX_DEVICES; s++) {
        void *tbl = acpi_find_table(signatures[s]);
        if (tbl) {
            struct device_info *dev = device_add(DEVICE_TYPE_UART);
            if (!dev) break;
            size_t i = 0;
            while (signatures[s][i] && i < ACPI_HID_MAX_LEN - 1) {
                dev->acpi.hid[i] = signatures[s][i];
                i++;
            }
            dev->acpi.hid[i] = '\0';
            device_set_name(dev, "uart-acpi");
        }
    }
}

static void acpi_pci_bridge_devices(void) {
    if (!acpi_is_ready()) return;
    acpi_for_each_table("PNP0A03", acpi_visitor_pci_bridge, NULL);
    acpi_for_each_table("PNP0A05", acpi_visitor_pci_bridge, NULL);
}

static void acpi_power_devices(void) {
    if (!acpi_is_ready()) return;
    struct device_info *dev = device_add(DEVICE_TYPE_ACPI);
    if (!dev) return;
    strncpy(dev->acpi.hid, "PNP0C0A", ACPI_HID_MAX_LEN);
    device_set_name(dev, "power-supply");
    g_acpi_count++;
}

static void acpi_sensor_devices(void) {
    if (!acpi_is_ready()) return;
    acpi_for_each_table("THDM", acpi_visitor_sensor, NULL);
    acpi_for_each_table("THDT", acpi_visitor_sensor, NULL);
}

static void acpi_network_devices(void) {
    if (!acpi_is_ready()) return;
    acpi_for_each_table("IBM", acpi_visitor_network, NULL);
}

static void acpi_storage_devices(void) {
    if (!acpi_is_ready()) return;
    acpi_for_each_table("ATA6", acpi_visitor_storage, NULL);
    acpi_for_each_table("ATAP", acpi_visitor_storage, NULL);
}

static void acpi_usb_devices(void) {
    if (!acpi_is_ready()) return;
    acpi_for_each_table("USB", acpi_visitor_usb, NULL);
    acpi_for_each_table("EHCI", acpi_visitor_usb, NULL);
    acpi_for_each_table("XHCI", acpi_visitor_usb, NULL);
}

static void acpi_display_devices(void) {
    if (!acpi_is_ready()) return;
    acpi_for_each_table("PCIC", acpi_visitor_display, NULL);
    acpi_for_each_table("GFX0", acpi_visitor_display, NULL);
}

static void acpi_audio_devices(void) {
    if (!acpi_is_ready()) return;
    acpi_for_each_table("HDAS", acpi_visitor_audio, NULL);
    acpi_for_each_table("AZAL", acpi_visitor_audio, NULL);
}

static void devman_enumerate_devices(void) {
    klog(KLOG_INFO, "devman: enumerating PCI devices...");
    pci_enumerate(pci_inspect_device, NULL);
    klogf(KLOG_OK, "devman: found %u PCI devices", g_pci_count);

    if (acpi_is_ready()) {
        klog(KLOG_INFO, "devman: enumerating ACPI devices...");
        acpi_madt_devices();
        acpi_ec_devices();
        acpi_battery_device();
        acpi_uart_devices();
        acpi_gpe_devices();
        acpi_pci_bridge_devices();
        acpi_power_devices();
        acpi_sensor_devices();
        acpi_network_devices();
        acpi_storage_devices();
        acpi_usb_devices();
        acpi_display_devices();
        acpi_audio_devices();
        klogf(KLOG_OK, "devman: found %u ACPI devices", g_acpi_count);
    }

    klog(KLOG_INFO, "devman: scanning ISA/legacy devices...");
    acpi_isa_enumerate();

    klogf(KLOG_OK, "devman: total devices=%u (pci=%u acpi=%u)",
          g_device_count, g_pci_count, g_acpi_count);
    g_enumerated = true;
}

void devman_init(void) {
    memset(g_devices, 0, sizeof(g_devices));
    g_device_count = 0;
    g_pci_count = 0;
    g_acpi_count = 0;
    g_enumerated = false;
    g_drivers = NULL;
    for (uint32_t i = 0; i < DEVICE_MANAGER_MAX_DEVICES; i++)
        device_zero(&g_devices[i]);
    klog(KLOG_INFO, "devman: initialized");
}

void devman_enumerate(void) {
    if (!g_enumerated) {
        devman_enumerate_devices();
    }
}

uint32_t devman_get_device_count(void) {
    if (!g_enumerated)
        devman_enumerate();
    return g_device_count;
}

const struct device_info *devman_get_device(uint32_t index) {
    if (!g_enumerated)
        devman_enumerate();
    if (index >= g_device_count)
        return NULL;
    return &g_devices[index];
}

bool devman_for_each(device_visitor_t visitor, void *ctx) {
    if (!g_enumerated)
        devman_enumerate();
    if (!visitor) return false;
    bool found = false;
    for (uint32_t i = 0; i < g_device_count; i++) {
        if (visitor(&g_devices[i], ctx))
            found = true;
    }
    return found;
}

const struct device_info *devman_find_pci(uint16_t vendor_id, uint16_t device_id) {
    if (!g_enumerated)
        devman_enumerate();
    for (uint32_t i = 0; i < g_device_count; i++) {
        if (g_devices[i].type == DEVICE_TYPE_PCI &&
            g_devices[i].pci.vendor_id == vendor_id &&
            g_devices[i].pci.device_id == device_id)
            return &g_devices[i];
    }
    return NULL;
}

const struct device_info *devman_find_by_class(enum device_class cls, uint8_t subclass) {
    if (!g_enumerated)
        devman_enumerate();
    for (uint32_t i = 0; i < g_device_count; i++) {
        if (g_devices[i].device_class == cls &&
            g_devices[i].pci.subclass == subclass)
            return &g_devices[i];
    }
    return NULL;
}

const struct device_info *devman_find_by_acpi_hid(const char *hid) {
    if (!g_enumerated || !hid) return NULL;
    for (uint32_t i = 0; i < g_device_count; i++) {
        if (g_devices[i].type == DEVICE_TYPE_ACPI &&
            strcmp(g_devices[i].acpi.hid, hid) == 0)
            return &g_devices[i];
    }
    return NULL;
}

int devman_register_driver(struct device_driver *driver) {
    if (!driver) return -1;
    driver->next = g_drivers;
    g_drivers = driver;
    klogf(KLOG_OK, "devman: registered driver '%s'", driver->name);
    return 0;
}

static bool driver_match_device(struct device_driver *driver, const struct device_info *dev) {
    if (driver->type != DEVICE_TYPE_UNKNOWN && driver->type != dev->type)
        return false;
    if (driver->device_class != CLASS_NONE && driver->device_class != dev->device_class)
        return false;
    if (driver->match) return driver->match(dev, NULL);
    if (dev->type == DEVICE_TYPE_PCI && driver->type == DEVICE_TYPE_PCI) {
        if (dev->pci.vendor_id == 0xFFFF) return true;
        return true;
    }
    if (dev->type == DEVICE_TYPE_ACPI && driver->type == DEVICE_TYPE_ACPI) {
        if (dev->acpi.hid[0]) return true;
        return true;
    }
    return false;
}

void devman_driver_probe_all(void) {
    if (!g_enumerated)
        devman_enumerate();
    struct device_driver *drv = g_drivers;
    while (drv) {
        int probed = 0;
        for (uint32_t i = 0; i < g_device_count; i++) {
            if (g_devices[i].driver_bound) continue;
            if (driver_match_device(drv, &g_devices[i])) {
                if (drv->probe) {
                    int rc = drv->probe(&g_devices[i]);
                    if (rc == 0) {
                        g_devices[i].driver_bound = true;
                        probed++;
                        klogf(KLOG_DEBUG, "devman: driver '%s' bound to device %u",
                              drv->name, g_devices[i].id);
                    }
                }
            }
        }
        if (probed > 0)
            klogf(KLOG_INFO, "devman: driver '%s' probed %d device(s)", drv->name, probed);
        drv = drv->next;
    }
}

bool devman_is_ready(void) {
    return g_enumerated || g_device_count > 0;
}

void devman_dump(void) {
    if (!g_enumerated)
        devman_enumerate();
    klogf(KLOG_INFO, "devman: device registry dump (%u devices)", g_device_count);
    for (uint32_t i = 0; i < g_device_count; i++) {
        const struct device_info *d = &g_devices[i];
        const char *tname = devman_device_type_name(d->type);
        klogf(KLOG_INFO, "devman: [%u] type=%s class=%d name='%s' enabled=%d bound=%d",
              i, tname, d->device_class, d->name, d->enabled, d->driver_bound);
        if (d->type == DEVICE_TYPE_PCI) {
            klogf(KLOG_DEBUG, "  PCI: %04x:%04x bus=%u slot=%u func=%u class=%02x:%02x:%02x rev=%02x",
                  d->pci.vendor_id, d->pci.device_id, d->pci.bus, d->pci.slot,
                  d->pci.function, d->pci.class_code, d->pci.subclass,
                  d->pci.programming_interface, d->pci.revision);
        }
        if (d->type == DEVICE_TYPE_ACPI && d->acpi.hid[0]) {
            klogf(KLOG_DEBUG, "  ACPI: HID='%s'", d->acpi.hid);
        }
        if (d->irq_count > 0) {
            klogf(KLOG_DEBUG, "  IRQ: count=%u lines=", d->irq_count);
            for (uint32_t j = 0; j < d->irq_count && j < 4; j++) {
                klogf(KLOG_DEBUG, " %u", d->irq_lines[j]);
            }
        }
    }
}

const char *devman_device_type_name(enum device_type type) {
    switch (type) {
        case DEVICE_TYPE_PCI: return "PCI";
        case DEVICE_TYPE_ACPI: return "ACPI";
        case DEVICE_TYPE_ISA: return "ISA";
        case DEVICE_TYPE_UART: return "UART";
        case DEVICE_TYPE_IRQ: return "IRQ";
        case DEVICE_TYPE_I2C: return "I2C";
        case DEVICE_TYPE_SPI: return "SPI";
        default: return "UNKNOWN";
    }
}

const char *devman_device_class_name(enum device_class cls) {
    switch (cls) {
        case CLASS_MASS_STORAGE: return "mass_storage";
        case CLASS_NETWORK: return "network";
        case CLASS_DISPLAY: return "display";
        case CLASS_AUDIO: return "audio";
        case CLASS_INPUT: return "input";
        case CLASS_BRIDGE: return "bridge";
        case CLASS_SERIAL: return "serial";
        case CLASS_USB: return "usb";
        case CLASS_WIRELESS: return "wireless";
        case CLASS_POWER: return "power";
        case CLASS_SENSOR: return "sensor";
        default: return "none";
    }
}

uint32_t devman_get_pci_device_count(void) {
    if (!g_enumerated)
        devman_enumerate();
    return g_pci_count;
}

uint32_t devman_get_acpi_device_count(void) {
    if (!g_enumerated)
        devman_enumerate();
    return g_acpi_count;
}
