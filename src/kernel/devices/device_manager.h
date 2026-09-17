#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define DEVICE_MANAGER_MAX_DEVICES 64
#define DEVICE_NAME_MAX_LEN 64
#define ACPI_HID_MAX_LEN 32

enum device_type {
    DEVICE_TYPE_UNKNOWN = 0,
    DEVICE_TYPE_PCI,
    DEVICE_TYPE_ACPI,
    DEVICE_TYPE_ISA,
    DEVICE_TYPE_UART,
    DEVICE_TYPE_IRQ,
    DEVICE_TYPE_I2C,
    DEVICE_TYPE_SPI,
};

enum device_class {
    CLASS_NONE = 0,
    CLASS_MASS_STORAGE,
    CLASS_NETWORK,
    CLASS_DISPLAY,
    CLASS_AUDIO,
    CLASS_INPUT,
    CLASS_BRIDGE,
    CLASS_SERIAL,
    CLASS_USB,
    CLASS_WIRELESS,
    CLASS_POWER,
    CLASS_SENSOR,
};

struct device_pci_info {
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t programming_interface;
    uint8_t revision;
    uint64_t base_addresses[6];
    uint8_t interrupt_pin;
    uint8_t interrupt_line;
};

struct device_acpi_info {
    char hid[ACPI_HID_MAX_LEN];
    char uid[32];
    uint32_t irq;
    uint64_t address;
    bool has_ec;
};

struct device_i2c_info {
    uint16_t address;
    uint32_t speed_hz;
    char controller[ACPI_HID_MAX_LEN];
};

struct device_info {
    uint32_t id;
    enum device_type type;
    enum device_class device_class;
    char name[DEVICE_NAME_MAX_LEN];
    bool enabled;
    bool driver_bound;
    struct device_pci_info pci;
    struct device_acpi_info acpi;
    struct device_i2c_info i2c;
    uint32_t irq_count;
    uint32_t irq_lines[4];
    uint64_t mmio_base;
    uint64_t mmio_size;
    uint32_t dma_channels;
    void *driver_data;
};

typedef bool (*device_visitor_t)(const struct device_info *dev, void *ctx);
typedef bool (*device_driver_match_t)(const struct device_info *dev, void *ctx);

struct device_driver {
    const char *name;
    enum device_type type;
    enum device_class device_class;
    device_driver_match_t match;
    int (*probe)(const struct device_info *dev);
    void (*remove)(const struct device_info *dev);
    struct device_driver *next;
};

void devman_init(void);
void devman_enumerate(void);
uint32_t devman_get_device_count(void);
const struct device_info *devman_get_device(uint32_t index);
bool devman_for_each(device_visitor_t visitor, void *ctx);
const struct device_info *devman_find_pci(uint16_t vendor_id, uint16_t device_id);
const struct device_info *devman_find_by_class(enum device_class cls, uint8_t subclass);
const struct device_info *devman_find_by_acpi_hid(const char *hid);
int devman_register_driver(struct device_driver *driver);
void devman_driver_probe_all(void);
bool devman_is_ready(void);
void devman_dump(void);
const char *devman_device_type_name(enum device_type type);
const char *devman_device_class_name(enum device_class cls);
uint32_t devman_get_pci_device_count(void);
uint32_t devman_get_acpi_device_count(void);
