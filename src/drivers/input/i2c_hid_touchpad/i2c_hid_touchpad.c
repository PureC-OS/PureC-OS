#include "i2c_hid_touchpad.h"
#include "arch/x86_64/idt/include/idt.h"
#include "arch/x86_64/mmio.h"
#include "drivers/interrupts/timer.h"
#include "drivers/mouse/ps2_mouse.h"
#include "drivers/pci/pci.h"
#include "kernel/diagnostics/klog.h"
#include "kernel/process/scheduler.h"
#include "kernel/smp/cpu.h"
#include "lib/string.h"
#include <acpi/acpi.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <uacpi/namespace.h>
#include <uacpi/types.h>
#include <uacpi/uacpi.h>

#define INTEL_VENDOR_ID 0x8086
#define PCI_CLASS_SERIAL_BUS 0x0C
#define PCI_SUBCLASS_SERIAL_OTHER 0x80
#define MAX_CONTROLLERS 8

#define LPSS_PRIV 0x200
#define LPSS_PRIV_RESETS 0x04
#define LPSS_PRIV_REMAP_LO 0x40
#define LPSS_PRIV_REMAP_HI 0x44
#define LPSS_CLOCK_HZ 133000000U
#define LPSS_SDA_HOLD_NS 42U
#define LPSS_SDA_FALL_NS 171U
#define LPSS_SCL_FALL_NS 208U

#define DW_CON 0x00
#define DW_TAR 0x04
#define DW_DATA_CMD 0x10
#define DW_SS_HCNT 0x14
#define DW_SS_LCNT 0x18
#define DW_FS_HCNT 0x1c
#define DW_FS_LCNT 0x20
#define DW_INTR_MASK 0x30
#define DW_RAW_INTR 0x34
#define DW_RX_TL 0x38
#define DW_TX_TL 0x3c
#define DW_CLR_INTR 0x40
#define DW_CLR_TX_ABRT 0x54
#define DW_ENABLE 0x6c
#define DW_STATUS 0x70
#define DW_SDA_HOLD 0x7c
#define DW_ABRT_SOURCE 0x80
#define DW_ENABLE_STATUS 0x9c
#define DW_COMP_VERSION 0xf8
#define DW_COMP_TYPE 0xfc
#define DW_COMP_TYPE_VALUE 0x44570140U
#define DW_SDA_HOLD_MIN_VERSION 0x3131312AU

#define DW_CON_MASTER 0x01U
#define DW_CON_SPEED_STD 0x02U
#define DW_CON_SPEED_FAST 0x04U
#define DW_CON_RESTART_EN 0x20U
#define DW_CON_SLAVE_DISABLE 0x40U
#define DW_CMD_READ 0x100U
#define DW_CMD_STOP 0x200U
#define DW_CMD_RESTART 0x400U
#define DW_INTR_TX_ABRT 0x40U
#define DW_STATUS_TFNF 0x02U
#define DW_STATUS_TFE 0x04U
#define DW_STATUS_RFNE 0x08U
#define DW_STATUS_MASTER_ACTIVITY 0x20U

#define HID_OPCODE_RESET 0x01
#define HID_OPCODE_SET_REPORT 0x03
#define HID_OPCODE_SET_POWER 0x08
#define HID_PWR_ON 0x00
#define HID_REPORT_FEATURE 3
#define HID_DESCRIPTOR_SIZE 30
#define HID_BUFFER_SIZE 4096
#define HID_MAX_FINGERS 10
#define HID_MAX_ID 32

#define IOAPIC_VECTOR 0x61
#define IOAPIC_RTE_MASK (1U << 16)
#define IOAPIC_RTE_LEVEL (1U << 15)
#define IOAPIC_RTE_ACTIVE_LOW (1U << 13)

#define TAP_MAX_MS 220U
#define TAP_MAX_TRAVEL 160
#define POLL_INTERVAL_MS 10U
#define IDLE_REPORTS_BEFORE_POLLING 300U

struct hid_descriptor {
    uint16_t length;
    uint16_t version;
    uint16_t report_descriptor_length;
    uint16_t report_descriptor_register;
    uint16_t input_register;
    uint16_t max_input_length;
    uint16_t output_register;
    uint16_t max_output_length;
    uint16_t command_register;
    uint16_t data_register;
    uint16_t vendor_id;
    uint16_t product_id;
    uint16_t version_id;
    uint32_t reserved;
} __attribute__((packed));

struct finger_layout {
    int32_t tip;
    int32_t confidence;
    int32_t contact_id;
    int32_t x;
    int32_t y;
};

struct touchpad_layout {
    bool valid;
    uint8_t report_id;
    uint32_t finger_count;
    struct finger_layout finger[HID_MAX_FINGERS];
    int32_t button;
    int32_t contact_count;
    uint32_t x_bits;
    uint32_t y_bits;
    int32_t x_max;
    int32_t y_max;
    int32_t mode_offset;
    uint8_t mode_report;
    uint32_t mode_bytes;
    int32_t switch_surface;
    int32_t switch_button;
    uint8_t switch_report;
    uint32_t switch_bytes;
};

struct contact {
    bool active;
    uint32_t id;
    int32_t x;
    int32_t y;
};

struct candidate {
    struct pci_device_info pci;
    uint32_t order;
};

struct controller_set {
    struct candidate list[MAX_CONTROLLERS];
    uint32_t count;
    int32_t preferred_slot;
    int32_t preferred_function;
};

static volatile uint8_t *dw;
static uint64_t controller_bar;
static struct hid_descriptor hid;
static struct touchpad_layout layout;
static uint8_t report_descriptor[HID_BUFFER_SIZE];
static uint8_t input_buffer[HID_BUFFER_SIZE];
static uint8_t command_buffer[64];

static volatile uint32_t *ioapic_regs;
static uint32_t irq_gsi;
static volatile bool irq_pending;
static volatile uint32_t irq_count;
static int touchpad_tid = -1;

struct acpi_target {
    bool found;
    char name[5];
    uint16_t address;
    uint32_t speed_hz;
    uint32_t irq;
    char controller[ACPI_NAMESPACE_ID_MAX];
};

static uint32_t dw_read(uint32_t offset)
{
    return *(volatile uint32_t *)(dw + offset);
}

static void dw_write(uint32_t offset, uint32_t value)
{
    *(volatile uint32_t *)(dw + offset) = value;
}

static void relax(void)
{
    __asm__ volatile("pause" ::: "memory");
}

static void busy_wait_ms(uint32_t milliseconds)
{
    uint64_t deadline = timer_ticks() + milliseconds + 1;
    while (timer_ticks() < deadline)
        relax();
}

static bool dw_wait(uint32_t reg, uint32_t mask, uint32_t value, uint32_t milliseconds)
{
    uint64_t deadline = timer_ticks() + milliseconds + 1;
    while ((dw_read(reg) & mask) != value)
    {
        if (timer_ticks() > deadline)
            return false;
        relax();
    }
    return true;
}

static void dw_disable(void)
{
    dw_write(DW_ENABLE, 0);
    (void)dw_wait(DW_ENABLE_STATUS, 1, 0, 100);
}

static uint32_t scl_hcnt(uint32_t high_ns, uint32_t fall_ns)
{
    uint64_t cycles = ((uint64_t)LPSS_CLOCK_HZ * (high_ns + fall_ns) + 500000000ULL) / 1000000000ULL;
    return (uint32_t)(cycles > 3 ? cycles - 3 : 1);
}

static uint32_t scl_lcnt(uint32_t low_ns, uint32_t fall_ns)
{
    uint64_t cycles = ((uint64_t)LPSS_CLOCK_HZ * (low_ns + fall_ns) + 500000000ULL) / 1000000000ULL;
    return (uint32_t)(cycles > 1 ? cycles - 1 : 1);
}

static bool dw_setup(uint32_t speed_hz)
{
    if (dw_read(DW_COMP_TYPE) != DW_COMP_TYPE_VALUE)
    {
        klogf(KLOG_WARN, "i2c-hid: DesignWare signature mismatch 0x%08x", dw_read(DW_COMP_TYPE));
        return false;
    }
    dw_disable();
    uint32_t con = DW_CON_MASTER | DW_CON_RESTART_EN | DW_CON_SLAVE_DISABLE;
    con |= speed_hz > 100000 ? DW_CON_SPEED_FAST : DW_CON_SPEED_STD;
    dw_write(DW_CON, con);
    dw_write(DW_SS_HCNT, scl_hcnt(4000, LPSS_SDA_FALL_NS));
    dw_write(DW_SS_LCNT, scl_lcnt(4700, LPSS_SCL_FALL_NS));
    dw_write(DW_FS_HCNT, scl_hcnt(600, LPSS_SDA_FALL_NS));
    dw_write(DW_FS_LCNT, scl_lcnt(1300, LPSS_SCL_FALL_NS));
    if (dw_read(DW_COMP_VERSION) >= DW_SDA_HOLD_MIN_VERSION)
        dw_write(DW_SDA_HOLD, (LPSS_CLOCK_HZ / 1000000U * LPSS_SDA_HOLD_NS + 500U) / 1000U);
    dw_write(DW_TX_TL, 0);
    dw_write(DW_RX_TL, 0);
    dw_write(DW_INTR_MASK, 0);
    klogf(KLOG_INFO, "i2c-hid: DesignWare v%08x %s mode FS hcnt=%u lcnt=%u",
          dw_read(DW_COMP_VERSION), speed_hz > 100000 ? "fast" : "standard",
          dw_read(DW_FS_HCNT), dw_read(DW_FS_LCNT));
    return true;
}

static bool dw_transfer(uint16_t address, const uint8_t *out, uint32_t out_length,
                        uint8_t *in, uint32_t in_length)
{
    dw_disable();
    dw_write(DW_TAR, address);
    dw_write(DW_ENABLE, 1);
    (void)dw_read(DW_ENABLE_STATUS);
    (void)dw_read(DW_CLR_INTR);
    uint64_t deadline = timer_ticks() + 40 + (out_length + in_length) / 3;
    bool failed = false;
    for (uint32_t i = 0; i < out_length && !failed; i++)
    {
        while (!(dw_read(DW_STATUS) & DW_STATUS_TFNF))
        {
            if (timer_ticks() > deadline || (dw_read(DW_RAW_INTR) & DW_INTR_TX_ABRT))
            {
                failed = true;
                break;
            }
            relax();
        }
        if (failed)
            break;
        uint32_t command = out[i];
        if (i + 1 == out_length && in_length == 0)
            command |= DW_CMD_STOP;
        dw_write(DW_DATA_CMD, command);
    }
    uint32_t issued = 0, received = 0;
    while (!failed && received < in_length)
    {
        if (timer_ticks() > deadline || (dw_read(DW_RAW_INTR) & DW_INTR_TX_ABRT))
        {
            failed = true;
            break;
        }
        if (issued < in_length && issued - received < 8 && (dw_read(DW_STATUS) & DW_STATUS_TFNF))
        {
            uint32_t command = DW_CMD_READ;
            if (issued == 0 && out_length)
                command |= DW_CMD_RESTART;
            if (issued + 1 == in_length)
                command |= DW_CMD_STOP;
            dw_write(DW_DATA_CMD, command);
            issued++;
            continue;
        }
        if (dw_read(DW_STATUS) & DW_STATUS_RFNE)
        {
            in[received++] = (uint8_t)dw_read(DW_DATA_CMD);
            continue;
        }
        relax();
    }
    if (!failed)
    {
        while (!(dw_read(DW_STATUS) & DW_STATUS_TFE) || (dw_read(DW_STATUS) & DW_STATUS_MASTER_ACTIVITY))
        {
            if (timer_ticks() > deadline + 20 || (dw_read(DW_RAW_INTR) & DW_INTR_TX_ABRT))
            {
                failed = true;
                break;
            }
            relax();
        }
    }
    if (failed)
    {
        uint32_t source = dw_read(DW_ABRT_SOURCE);
        (void)dw_read(DW_CLR_TX_ABRT);
        dw_disable();
        klogf(KLOG_WARN, "i2c-hid: transfer addr=0x%02x w=%u r=%u failed abort_source=0x%x",
              address, out_length, in_length, source);
        return false;
    }
    return true;
}

static void lpss_reset(const struct pci_device_info *pci)
{
    (void)pci;
    volatile uint32_t *resets = (volatile uint32_t *)(dw + LPSS_PRIV + LPSS_PRIV_RESETS);
    volatile uint32_t *remap_lo = (volatile uint32_t *)(dw + LPSS_PRIV + LPSS_PRIV_REMAP_LO);
    volatile uint32_t *remap_hi = (volatile uint32_t *)(dw + LPSS_PRIV + LPSS_PRIV_REMAP_HI);
    *resets = 0;
    busy_wait_ms(2);
    *resets = 0x7;
    *remap_lo = (uint32_t)controller_bar;
    *remap_hi = (uint32_t)(controller_bar >> 32);
    busy_wait_ms(2);
}

static void put16(uint8_t *buffer, uint16_t value)
{
    buffer[0] = (uint8_t)value;
    buffer[1] = (uint8_t)(value >> 8);
}

static uint16_t get16(const uint8_t *buffer)
{
    return (uint16_t)(buffer[0] | (buffer[1] << 8));
}

static bool hid_read_register(uint16_t address, uint16_t reg, void *out, uint32_t length)
{
    uint8_t request[2];
    put16(request, reg);
    return dw_transfer(address, request, 2, (uint8_t *)out, length);
}

static bool hid_command(uint16_t address, uint8_t first, uint8_t opcode)
{
    put16(command_buffer, hid.command_register);
    command_buffer[2] = first;
    command_buffer[3] = opcode;
    return dw_transfer(address, command_buffer, 4, NULL, 0);
}

static bool hid_set_feature(uint16_t address, uint8_t report_id, const uint8_t *data, uint32_t length)
{
    uint32_t n = 0;
    put16(command_buffer + n, hid.command_register);
    n += 2;
    command_buffer[n++] = (uint8_t)((HID_REPORT_FEATURE << 4) | (report_id < 0x0F ? report_id : 0x0F));
    command_buffer[n++] = HID_OPCODE_SET_REPORT;
    if (report_id >= 0x0F)
        command_buffer[n++] = report_id;
    put16(command_buffer + n, hid.data_register);
    n += 2;
    put16(command_buffer + n, (uint16_t)(2 + 1 + length));
    n += 2;
    command_buffer[n++] = report_id;
    if (n + length > sizeof(command_buffer))
        return false;
    memcpy(command_buffer + n, data, length);
    n += length;
    return dw_transfer(address, command_buffer, n, NULL, 0);
}

static uint32_t get_bits(const uint8_t *data, uint32_t offset, uint32_t size)
{
    uint32_t value = 0;
    for (uint32_t i = 0; i < size && i < 32; i++)
    {
        uint32_t bit = offset + i;
        value |= (uint32_t)((data[bit / 8] >> (bit % 8)) & 1U) << i;
    }
    return value;
}

static void parse_report_descriptor(const uint8_t *d, uint32_t length)
{
    memset(&layout, 0, sizeof(layout));
    layout.button = layout.contact_count = layout.mode_offset = -1;
    layout.switch_surface = layout.switch_button = -1;
    uint32_t usage_page = 0, report_size = 0, report_count = 0, report_id = 0;
    int32_t logical_max = 0;
    uint32_t usages[64], usage_count = 0, usage_min = 0, usage_max = 0;
    uint32_t input_bits[HID_MAX_ID] = {0};
    uint32_t feature_bits[HID_MAX_ID] = {0};
    int32_t finger = -1;
    uint32_t pos = 0;
    while (pos < length)
    {
        uint8_t prefix = d[pos++];
        if (prefix == 0xFE)
        {
            if (pos >= length)
                break;
            pos += 2 + d[pos];
            continue;
        }
        uint32_t size = prefix & 3U;
        if (size == 3)
            size = 4;
        if (pos + size > length)
            break;
        uint32_t type = (prefix >> 2) & 3U, tag = prefix >> 4;
        uint32_t value = 0;
        for (uint32_t i = 0; i < size; i++)
            value |= (uint32_t)d[pos + i] << (8 * i);
        int32_t signed_value = (int32_t)value;
        if (size == 1)
            signed_value = (int8_t)value;
        else if (size == 2)
            signed_value = (int16_t)value;
        pos += size;
        if (type == 1)
        {
            if (tag == 0)
                usage_page = value;
            else if (tag == 2)
                logical_max = signed_value;
            else if (tag == 7)
                report_size = value;
            else if (tag == 8)
                report_id = value < HID_MAX_ID ? value : 0;
            else if (tag == 9)
                report_count = value;
        }
        else if (type == 2)
        {
            if (tag == 0 && usage_count < 64)
                usages[usage_count++] = value;
            else if (tag == 1)
                usage_min = value;
            else if (tag == 2)
                usage_max = value;
        }
        else if (type == 0 && (tag == 8 || tag == 11))
        {
            bool is_input = tag == 8;
            bool constant = value & 1U;
            uint32_t *bits = is_input ? &input_bits[report_id] : &feature_bits[report_id];
            if (usage_count == 0 && usage_max >= usage_min && usage_max)
                for (uint32_t u = usage_min; u <= usage_max && usage_count < 64; u++)
                    usages[usage_count++] = u;
            for (uint32_t i = 0; i < report_count; i++)
            {
                uint32_t offset = *bits + i * report_size;
                uint32_t usage = usage_count ? usages[i < usage_count ? i : usage_count - 1] : 0;
                if (!constant && is_input && usage_page == 0x0D)
                {
                    if (usage == 0x42)
                    {
                        if (finger + 1 < HID_MAX_FINGERS)
                        {
                            finger++;
                            layout.report_id = (uint8_t)report_id;
                            layout.finger[finger].tip = (int32_t)offset;
                            layout.finger[finger].confidence = layout.finger[finger].contact_id = -1;
                            layout.finger[finger].x = layout.finger[finger].y = -1;
                            layout.finger_count = (uint32_t)finger + 1;
                        }
                    }
                    else if (finger >= 0 && usage == 0x47)
                        layout.finger[finger].confidence = (int32_t)offset;
                    else if (finger >= 0 && usage == 0x51)
                        layout.finger[finger].contact_id = (int32_t)offset;
                    else if (usage == 0x54)
                        layout.contact_count = (int32_t)offset;
                }
                else if (!constant && is_input && usage_page == 0x01 && finger >= 0)
                {
                    if (usage == 0x30)
                    {
                        layout.finger[finger].x = (int32_t)offset;
                        layout.x_bits = report_size;
                        layout.x_max = logical_max;
                    }
                    else if (usage == 0x31)
                    {
                        layout.finger[finger].y = (int32_t)offset;
                        layout.y_bits = report_size;
                        layout.y_max = logical_max;
                    }
                }
                else if (!constant && is_input && usage_page == 0x09 && usage == 1 && layout.button < 0 &&
                         report_id == layout.report_id && finger >= 0)
                    layout.button = (int32_t)offset;
                else if (!is_input && usage_page == 0x0D)
                {
                    if (usage == 0x52)
                    {
                        layout.mode_report = (uint8_t)report_id;
                        layout.mode_offset = (int32_t)offset;
                    }
                    else if (usage == 0x57)
                    {
                        layout.switch_report = (uint8_t)report_id;
                        layout.switch_surface = (int32_t)offset;
                    }
                    else if (usage == 0x58)
                        layout.switch_button = (int32_t)offset;
                }
            }
            *bits += report_count * report_size;
        }
        if (type == 0)
            usage_count = usage_min = usage_max = 0;
    }
    layout.valid = layout.finger_count && layout.report_id && layout.finger[0].x >= 0 && layout.finger[0].y >= 0;
    if (layout.mode_offset >= 0)
        layout.mode_bytes = (feature_bits[layout.mode_report] + 7) / 8;
    if (layout.switch_surface >= 0)
        layout.switch_bytes = (feature_bits[layout.switch_report] + 7) / 8;
    klogf(KLOG_INFO, "i2c-hid: touchpad layout valid=%d report=%u fingers=%u x_max=%d y_max=%d button_bit=%d "
                     "mode_report=%u switch_report=%u",
          layout.valid, layout.report_id, layout.finger_count, layout.x_max, layout.y_max,
          layout.button, layout.mode_report, layout.switch_report);
}

static bool hid_wait_reset_ack(uint16_t address, uint32_t timeout_ms)
{
    uint32_t length = hid.max_input_length;
    if (length < 2 || length > sizeof(input_buffer))
        length = 64;
    uint64_t deadline = timer_ticks() + timeout_ms;
    while (timer_ticks() < deadline)
    {
        scheduler_sleep(10);
        if (dw_transfer(address, NULL, 0, input_buffer, length) && get16(input_buffer) == 0)
            return true;
    }
    return false;
}

static bool hid_start(uint16_t address, uint16_t descriptor_register)
{
    if (!hid_read_register(address, descriptor_register, &hid, HID_DESCRIPTOR_SIZE))
        return false;
    if (hid.version != 0x0100 || hid.length != HID_DESCRIPTOR_SIZE)
    {
        klogf(KLOG_WARN, "i2c-hid: bad HID descriptor version=0x%04x length=%u", hid.version, hid.length);
        return false;
    }
    klogf(KLOG_OK, "i2c-hid: HID descriptor vid=%04x pid=%04x report_desc=%u bytes max_input=%u cmd=0x%04x "
                   "data=0x%04x",
          hid.vendor_id, hid.product_id, hid.report_descriptor_length, hid.max_input_length,
          hid.command_register, hid.data_register);
    if (hid.report_descriptor_length == 0 || hid.report_descriptor_length > sizeof(report_descriptor) ||
        hid.max_input_length < 4 || hid.max_input_length > sizeof(input_buffer))
        return false;
    if (!hid_command(address, HID_PWR_ON, HID_OPCODE_SET_POWER))
        return false;
    busy_wait_ms(2);
    if (!hid_command(address, 0, HID_OPCODE_RESET))
        return false;
    bool acknowledged = hid_wait_reset_ack(address, 1000);
    klogf(acknowledged ? KLOG_OK : KLOG_WARN, "i2c-hid: reset %s", acknowledged ? "acknowledged" : "not acknowledged");
    (void)hid_command(address, HID_PWR_ON, HID_OPCODE_SET_POWER);
    if (!hid_read_register(address, hid.report_descriptor_register, report_descriptor,
                           hid.report_descriptor_length))
        return false;
    parse_report_descriptor(report_descriptor, hid.report_descriptor_length);
    if (!layout.valid)
        return false;
    if (layout.mode_offset >= 0 && layout.mode_bytes && layout.mode_bytes <= 8)
    {
        uint8_t data[8] = {0};
        data[(uint32_t)layout.mode_offset / 8] = 3;
        bool ok = hid_set_feature(address, layout.mode_report, data, layout.mode_bytes);
        klogf(ok ? KLOG_OK : KLOG_WARN, "i2c-hid: input mode touchpad %s", ok ? "set" : "failed");
    }
    if (layout.switch_surface >= 0 && layout.switch_bytes && layout.switch_bytes <= 8)
    {
        uint8_t data[8] = {0};
        data[(uint32_t)layout.switch_surface / 8] |= (uint8_t)(1U << ((uint32_t)layout.switch_surface % 8));
        if (layout.switch_button >= 0)
            data[(uint32_t)layout.switch_button / 8] |= (uint8_t)(1U << ((uint32_t)layout.switch_button % 8));
        bool ok = hid_set_feature(address, layout.switch_report, data, layout.switch_bytes);
        klogf(ok ? KLOG_OK : KLOG_WARN, "i2c-hid: surface/button switch %s", ok ? "set" : "failed");
    }
    return true;
}

static uacpi_iteration_decision find_node(void *user, uacpi_namespace_node *node, uacpi_u32 depth)
{
    (void)depth;
    struct { const char *name; uacpi_namespace_node *node; } *search = user;
    uacpi_object_name name = uacpi_namespace_node_name(node);
    if (memcmp(name.text, search->name, 4) != 0)
        return UACPI_ITERATION_DECISION_CONTINUE;
    search->node = node;
    return UACPI_ITERATION_DECISION_BREAK;
}

static uint16_t acpi_descriptor_register(const char *name)
{
    struct { const char *name; uacpi_namespace_node *node; } search = {name, NULL};
    if (!acpi_uacpi_full_ready())
        return 1;
    (void)uacpi_namespace_for_each_child(uacpi_namespace_root(), find_node, NULL,
                                         UACPI_OBJECT_DEVICE_BIT, UACPI_MAX_DEPTH_ANY, &search);
    if (!search.node)
        return 1;
    (void)uacpi_eval(search.node, "_PS0", NULL, NULL);
    static const uint8_t guid[16] = {0xf7, 0xf6, 0xdf, 0x3c, 0x67, 0x42, 0x55, 0x45,
                                     0xad, 0x05, 0xb3, 0x0a, 0x3d, 0x89, 0x38, 0xde};
    uacpi_data_view view;
    view.const_data = guid;
    view.length = sizeof(guid);
    uacpi_object *objects[4];
    objects[0] = uacpi_object_create_buffer(view);
    objects[1] = uacpi_object_create_integer(1);
    objects[2] = uacpi_object_create_integer(1);
    uacpi_object_array empty = {NULL, 0};
    objects[3] = uacpi_object_create_package(empty);
    uint16_t result = 1;
    if (objects[0] && objects[1] && objects[2] && objects[3])
    {
        uacpi_object_array arguments = {objects, 4};
        uacpi_object *returned = NULL;
        if (uacpi_eval(search.node, "_DSM", &arguments, &returned) == UACPI_STATUS_OK && returned)
        {
            uacpi_u64 value = 0;
            if (uacpi_object_get_integer(returned, &value) == UACPI_STATUS_OK && value && value <= 0xFFFF)
                result = (uint16_t)value;
            uacpi_object_unref(returned);
        }
    }
    for (int i = 0; i < 4; i++)
        if (objects[i])
            uacpi_object_unref(objects[i]);
    return result;
}

static bool acpi_visitor(const struct acpi_namespace_device *device, void *context)
{
    struct acpi_target *target = context;
    if (target->found || !device->has_i2c)
        return true;
    if (strcmp(device->hid, "PNP0C50") != 0 && strcmp(device->hid, "ACPI0C50") != 0)
        return true;
    target->found = true;
    memcpy(target->name, device->name, 5);
    target->address = device->i2c_address;
    target->speed_hz = device->i2c_speed_hz ? device->i2c_speed_hz : 400000;
    target->irq = device->irq;
    memcpy(target->controller, device->i2c_controller, sizeof(target->controller));
    return true;
}

static void collect_controllers(const struct pci_device_info *pci, void *context)
{
    struct controller_set *c = context;
    if (c->count == MAX_CONTROLLERS || pci->vendor_id != INTEL_VENDOR_ID ||
        pci->class_code != PCI_CLASS_SERIAL_BUS || pci->subclass != PCI_SUBCLASS_SERIAL_OTHER)
        return;
    struct candidate *entry = &c->list[c->count++];
    entry->pci = *pci;
    entry->order = (pci->slot == c->preferred_slot && pci->function == c->preferred_function) ? 0 : 1 + c->count;
}

static void controller_from_path(const char *path, int32_t *slot, int32_t *function)
{
    *slot = *function = -1;
    for (uint32_t i = 0; path[i] && path[i + 1] && path[i + 2] && path[i + 3]; i++)
    {
        if (path[i] == 'I' && path[i + 1] == '2' && path[i + 2] == 'C' && path[i + 3] >= '0' && path[i + 3] <= '5')
        {
            int32_t n = path[i + 3] - '0';
            *slot = n < 4 ? 0x15 : 0x19;
            *function = n < 4 ? n : n - 4;
            return;
        }
    }
}

static bool ioapic_setup(uint32_t gsi)
{
    struct acpi_madt_info madt;
    if (!acpi_get_madt(&madt) || madt.ioapic_count != 1 || !madt.ioapic_first_addr)
        return false;
    ioapic_regs = (volatile uint32_t *)mmio_map(madt.ioapic_first_addr, 0x1000);
    if (!ioapic_regs)
        return false;
    ioapic_regs[0] = 1;
    uint32_t version = ioapic_regs[4];
    uint32_t last = (version >> 16) & 0xFF;
    if (gsi > last)
    {
        ioapic_regs = NULL;
        return false;
    }
    const struct cpu_info *bsp = cpu_get_info(0);
    uint32_t destination = bsp ? bsp->lapic_id : madt.lapic_ids[0];
    ioapic_regs[0] = 0x10 + gsi * 2 + 1;
    ioapic_regs[4] = destination << 24;
    ioapic_regs[0] = 0x10 + gsi * 2;
    ioapic_regs[4] = IOAPIC_VECTOR | IOAPIC_RTE_LEVEL | IOAPIC_RTE_ACTIVE_LOW | IOAPIC_RTE_MASK;
    irq_gsi = gsi;
    klogf(KLOG_INFO, "i2c-hid: IO-APIC pin %u (max %u) -> vector 0x%02x cpu lapic %u", gsi, last, IOAPIC_VECTOR, destination);
    return true;
}

static void ioapic_set_mask(bool masked)
{
    if (!ioapic_regs)
        return;
    ioapic_regs[0] = 0x10 + irq_gsi * 2;
    uint32_t value = ioapic_regs[4];
    value = masked ? (value | IOAPIC_RTE_MASK) : (value & ~IOAPIC_RTE_MASK);
    ioapic_regs[0] = 0x10 + irq_gsi * 2;
    ioapic_regs[4] = value;
}

static void touchpad_interrupt(void *context)
{
    (void)context;
    ioapic_set_mask(true);
    irq_count++;
    irq_pending = true;
}

static void emit_motion(uint8_t buttons, int32_t dx, int32_t dy)
{
    do
    {
        int32_t step_x = dx > 127 ? 127 : dx < -127 ? -127 : dx;
        int32_t step_y = dy > 127 ? 127 : dy < -127 ? -127 : dy;
        mouse_handle_relative(buttons, (int8_t)step_x, (int8_t)step_y);
        dx -= step_x;
        dy -= step_y;
    } while (dx || dy);
}

struct gesture_state {
    bool touching;
    uint32_t primary_id;
    bool primary_valid;
    int32_t last_x, last_y;
    int32_t acc_x, acc_y;
    uint64_t touch_start;
    int32_t travel;
    uint32_t max_fingers;
    uint8_t physical_buttons;
    uint32_t reports;
};

static struct gesture_state gesture;

static int32_t absolute(int32_t value)
{
    return value < 0 ? -value : value;
}

static int32_t scale_axis(int32_t delta, int32_t *accumulator)
{
    int32_t magnitude = absolute(delta);
    int32_t gain = 5 + (magnitude > 120 ? 120 : magnitude) / 24;
    *accumulator += delta * gain;
    int32_t out = *accumulator / 16;
    *accumulator -= out * 16;
    return out;
}

static void click(uint8_t buttons)
{
    mouse_handle_relative(buttons, 0, 0);
    scheduler_sleep(25);
    mouse_handle_relative(0, 0, 0);
}

static void process_report(const uint8_t *data, uint32_t length)
{
    if (length < 1 + 1 || data[0] != layout.report_id)
        return;
    const uint8_t *payload = data + 1;
    uint32_t payload_bits = (length - 1) * 8;
    struct contact contacts[HID_MAX_FINGERS];
    uint32_t active = 0;
    for (uint32_t i = 0; i < layout.finger_count; i++)
    {
        const struct finger_layout *f = &layout.finger[i];
        contacts[i].active = false;
        if (f->tip < 0 || f->x < 0 || f->y < 0 || (uint32_t)f->x + layout.x_bits > payload_bits ||
            (uint32_t)f->y + layout.y_bits > payload_bits)
            continue;
        bool tip = get_bits(payload, (uint32_t)f->tip, 1);
        bool confident = f->confidence < 0 || get_bits(payload, (uint32_t)f->confidence, 1);
        if (!tip || !confident)
            continue;
        contacts[i].active = true;
        contacts[i].id = f->contact_id >= 0 ? get_bits(payload, (uint32_t)f->contact_id, 4) : i;
        contacts[i].x = (int32_t)get_bits(payload, (uint32_t)f->x, layout.x_bits);
        contacts[i].y = (int32_t)get_bits(payload, (uint32_t)f->y, layout.y_bits);
        active++;
    }
    uint8_t physical = 0;
    if (layout.button >= 0 && (uint32_t)layout.button < payload_bits && get_bits(payload, (uint32_t)layout.button, 1))
        physical = 1;
    if (physical != gesture.physical_buttons)
    {
        gesture.physical_buttons = physical;
        mouse_handle_relative(physical, 0, 0);
    }
    gesture.reports++;
    if (active == 0)
    {
        if (gesture.touching)
        {
            uint64_t duration = timer_ticks() - gesture.touch_start;
            if (!physical && duration <= TAP_MAX_MS && gesture.travel <= TAP_MAX_TRAVEL)
            {
                if (gesture.max_fingers == 1)
                    click(1);
                else if (gesture.max_fingers == 2)
                    click(2);
            }
        }
        gesture.touching = gesture.primary_valid = false;
        gesture.acc_x = gesture.acc_y = 0;
        return;
    }
    if (!gesture.touching)
    {
        gesture.touching = true;
        gesture.touch_start = timer_ticks();
        gesture.travel = 0;
        gesture.max_fingers = 0;
        gesture.primary_valid = false;
    }
    if (active > gesture.max_fingers)
        gesture.max_fingers = active;
    const struct contact *primary = NULL;
    for (uint32_t i = 0; i < layout.finger_count; i++)
        if (contacts[i].active && gesture.primary_valid && contacts[i].id == gesture.primary_id)
            primary = &contacts[i];
    if (!primary)
    {
        for (uint32_t i = 0; i < layout.finger_count; i++)
            if (contacts[i].active)
            {
                primary = &contacts[i];
                break;
            }
        gesture.primary_id = primary->id;
        gesture.primary_valid = true;
        gesture.last_x = primary->x;
        gesture.last_y = primary->y;
        return;
    }
    int32_t dx = primary->x - gesture.last_x;
    int32_t dy = primary->y - gesture.last_y;
    gesture.last_x = primary->x;
    gesture.last_y = primary->y;
    gesture.travel += absolute(dx) + absolute(dy);
    if (active >= 2)
        return;
    int32_t out_x = scale_axis(dx, &gesture.acc_x);
    int32_t out_y = scale_axis(dy, &gesture.acc_y);
    if (out_x || out_y)
        emit_motion(physical, out_x, out_y);
}

static bool read_input(uint16_t address)
{
    uint32_t length = hid.max_input_length;
    if (!dw_transfer(address, NULL, 0, input_buffer, length))
        return false;
    uint32_t reported = get16(input_buffer);
    if (reported < 3 || reported > length)
        return false;
    static uint32_t logged;
    if (logged < 6)
    {
        logged++;
        klogf(KLOG_INFO, "i2c-hid: report len=%u id=%u b=%02x %02x %02x %02x %02x %02x %02x %02x", reported,
              input_buffer[2], input_buffer[3], input_buffer[4], input_buffer[5], input_buffer[6],
              input_buffer[7], input_buffer[8], input_buffer[9], input_buffer[10]);
    }
    process_report(input_buffer + 2, reported - 2);
    return true;
}

static bool bring_up(const struct acpi_target *target, uint16_t descriptor_register)
{
    struct controller_set controllers;
    memset(&controllers, 0, sizeof(controllers));
    controller_from_path(target->controller, &controllers.preferred_slot, &controllers.preferred_function);
    pci_enumerate(collect_controllers, &controllers);
    klogf(KLOG_INFO, "i2c-hid: %u Intel serial-bus controller(s), ACPI path '%s'", controllers.count, target->controller);
    for (uint32_t pass = 0; pass < controllers.count; pass++)
    {
        uint32_t best = MAX_CONTROLLERS, best_order = 0xFFFFFFFFU;
        for (uint32_t i = 0; i < controllers.count; i++)
            if (controllers.list[i].order < best_order)
            {
                best = i;
                best_order = controllers.list[i].order;
            }
        if (best == MAX_CONTROLLERS)
            break;
        struct pci_device_info pci = controllers.list[best].pci;
        controllers.list[best].order = 0xFFFFFFFFU;
        uint64_t bar = pci_read_bar(pci.bus, pci.slot, pci.function, 0);
        if ((bar & 1) || !(bar & ~0xFULL))
            continue;
        controller_bar = bar & ~0xFULL;
        (void)pci_update_command(&pci, PCI_COMMAND_MEMORY | PCI_COMMAND_BUS_MASTER, 0);
        dw = (volatile uint8_t *)mmio_map(controller_bar, 0x1000);
        if (!dw)
            continue;
        klogf(KLOG_INFO, "i2c-hid: trying controller %02x:%02x.%u id=%04x:%04x bar=0x%llx", pci.bus, pci.slot,
              pci.function, pci.vendor_id, pci.device_id, controller_bar);
        lpss_reset(&pci);
        if (!dw_setup(target->speed_hz))
            continue;
        if (hid_start(target->address, descriptor_register))
            return true;
    }
    return false;
}

static void touchpad_thread(void *argument)
{
    (void)argument;
    for (uint32_t waited = 0; waited < 150 && !(acpi_is_ready() && acpi_uacpi_full_ready()); waited++)
        scheduler_sleep(200);
    struct acpi_target target;
    memset(&target, 0, sizeof(target));
    (void)acpi_for_each_namespace_device(acpi_visitor, &target);
    if (!target.found)
    {
        klog(KLOG_INFO, "i2c-hid: no ACPI HID-over-I2C device, touchpad driver idle");
        scheduler_exit();
        return;
    }
    uint16_t descriptor_register = acpi_descriptor_register(target.name);
    klogf(KLOG_INFO, "i2c-hid: ACPI device %s addr=0x%02x speed=%u irq=%u descriptor_register=0x%04x", target.name,
          target.address, target.speed_hz, target.irq, descriptor_register);
    if (!bring_up(&target, descriptor_register))
    {
        klog(KLOG_WARN, "i2c-hid: touchpad bring-up failed, USB/PS2 mouse remains in use");
        scheduler_exit();
        return;
    }
    bool use_irq = false;
    if (target.irq && ioapic_setup(target.irq))
    {
        idt_set_vector_handler(IOAPIC_VECTOR, touchpad_interrupt, NULL);
        use_irq = true;
        irq_pending = false;
        ioapic_set_mask(false);
    }
    klogf(KLOG_OK, "i2c-hid: touchpad running, %s", use_irq ? "interrupt driven" : "polling");
    uint32_t idle_streak = 0;
    uint64_t last_poll = 0;
    uint64_t stats_time = timer_ticks();
    for (;;)
    {
        scheduler_sleep(use_irq ? 2 : 4);
        bool run = false;
        if (use_irq)
        {
            if (irq_pending)
            {
                irq_pending = false;
                run = true;
            }
        }
        else if (timer_ticks() - last_poll >= POLL_INTERVAL_MS)
        {
            last_poll = timer_ticks();
            run = true;
        }
        if (run)
        {
            bool got = read_input(target.address);
            if (use_irq)
            {
                ioapic_set_mask(false);
                idle_streak = got ? 0 : idle_streak + 1;
                if (idle_streak >= IDLE_REPORTS_BEFORE_POLLING)
                {
                    ioapic_set_mask(true);
                    use_irq = false;
                    klog(KLOG_WARN, "i2c-hid: interrupt line looks stuck, switching to polling");
                }
            }
        }
        if (timer_ticks() - stats_time >= 60000)
        {
            stats_time = timer_ticks();
            klogf(KLOG_INFO, "i2c-hid: stats irq=%u reports=%u mode=%s", irq_count, gesture.reports,
                  use_irq ? "irq" : "poll");
        }
    }
}

bool purec_i2c_hid_touchpad_start(void)
{
    if (touchpad_tid >= 0)
        return true;
    touchpad_tid = scheduler_create_thread(touchpad_thread, NULL, "touchpad", 2, -1);
    if (touchpad_tid < 0)
    {
        klog(KLOG_WARN, "i2c-hid: cannot create touchpad thread");
        return false;
    }
    scheduler_set_qos(touchpad_tid, SCHED_QOS_USER_INTERACTIVE);
    return true;
}
