#include "vbox_mouse.h"
#include "ps2_mouse.h"
#include "../pci/pci.h"
#include "../../mm/pmm.h"
#include "../../kernel/diagnostics/klog.h"
#include "../../lib/string.h"
#include <stdint.h>

#define VBOX_PCI_VENDOR 0x80EEu
#define VBOX_PCI_DEVICE 0xCAFEu

#define VMMDEV_HEADER_VERSION 0x10001u
#define VMMDEV_INTERFACE_VERSION 0x00010003u
#define VMMDEVREQ_GET_MOUSE_STATUS 1u
#define VMMDEVREQ_SET_MOUSE_STATUS 2u
#define VMMDEVREQ_REPORT_GUEST_INFO 50u

#define VMMDEV_MOUSE_GUEST_CAN_ABSOLUTE 0x01u
#define VMMDEV_MOUSE_HOST_WANTS_ABSOLUTE 0x02u
#define VMMDEV_MOUSE_NEW_PROTOCOL 0x10u

#define VMMDEV_ALLOC_TRIES 64

struct vmmdev_header
{
    uint32_t size;
    uint32_t version;
    uint32_t request_type;
    int32_t rc;
    uint32_t reserved1;
    uint32_t requestor;
};

struct vmmdev_guest_info
{
    struct vmmdev_header header;
    uint32_t interface_version;
    uint32_t os_type;
};

struct vmmdev_mouse_status
{
    struct vmmdev_header header;
    uint32_t features;
    int32_t x;
    int32_t y;
};

static bool ready;
static volatile bool busy;
static bool host_wants_absolute;
static uint16_t io_base;
static uint64_t req_phys;
static volatile uint8_t *req_virt;

struct vbox_probe
{
    bool found;
    struct pci_device_info device;
};

static void probe_visitor(const struct pci_device_info *device, void *context)
{
    struct vbox_probe *probe = (struct vbox_probe *)context;
    if (!device || probe->found)
        return;
    if (device->vendor_id == VBOX_PCI_VENDOR && device->device_id == VBOX_PCI_DEVICE)
    {
        probe->found = true;
        probe->device = *device;
    }
}

static inline void outl(uint16_t port, uint32_t value)
{
    __asm__ volatile("outl %0,%1" ::"a"(value), "Nd"(port) : "memory");
}

static int32_t submit(uint32_t type, uint32_t size)
{
    struct vmmdev_header *header = (struct vmmdev_header *)req_virt;
    header->size = size;
    header->version = VMMDEV_HEADER_VERSION;
    header->request_type = type;
    header->rc = -1;
    header->reserved1 = 0;
    header->requestor = 0;
    __asm__ volatile("" ::: "memory");
    outl(io_base, (uint32_t)req_phys);
    __asm__ volatile("" ::: "memory");
    return ((volatile struct vmmdev_header *)req_virt)->rc;
}

static bool alloc_request_page(void)
{
    uint64_t rejected[VMMDEV_ALLOC_TRIES];
    uint32_t rejected_count = 0;
    uint64_t page = 0;
    for (uint32_t i = 0; i < VMMDEV_ALLOC_TRIES; i++)
    {
        uint64_t candidate = pmm_allocate_page();
        if (!candidate)
            break;
        if (candidate < 0x100000000ULL)
        {
            page = candidate;
            break;
        }
        rejected[rejected_count++] = candidate;
    }
    for (uint32_t i = 0; i < rejected_count; i++)
        pmm_free_page(rejected[i]);
    if (!page)
        return false;
    req_phys = page;
    req_virt = (volatile uint8_t *)pmm_physical_to_virtual(page);
    memset((void *)req_virt, 0, 4096);
    return true;
}

bool vbox_mouse_available(void) { return ready; }

bool vbox_mouse_init(void)
{
    struct vbox_probe probe = {0};
    pci_enumerate(probe_visitor, &probe);
    if (!probe.found)
        return false;
    const struct pci_device_info *d = &probe.device;
    if (!pci_update_command(d, PCI_COMMAND_IO_SPACE | PCI_COMMAND_BUS_MASTER, 0))
    {
        klog(KLOG_WARN, "vboxmouse: PCI enable failed");
        return false;
    }
    uint64_t bar0 = pci_read_bar(d->bus, d->slot, d->function, 0);
    io_base = (uint16_t)(bar0 & ~0x3ULL);
    if (!io_base)
    {
        klog(KLOG_WARN, "vboxmouse: BAR0 has no I/O port");
        return false;
    }
    if (!alloc_request_page())
    {
        klog(KLOG_WARN, "vboxmouse: no request page below 4 GiB");
        return false;
    }

    struct vmmdev_guest_info *info = (struct vmmdev_guest_info *)req_virt;
    info->interface_version = VMMDEV_INTERFACE_VERSION;
    info->os_type = 0;
    int32_t rc = submit(VMMDEVREQ_REPORT_GUEST_INFO, sizeof(*info));
    if (rc < 0)
        klogf(KLOG_WARN, "vboxmouse: guest info rc=%d", rc);

    struct vmmdev_mouse_status *mouse = (struct vmmdev_mouse_status *)req_virt;
    mouse->features = VMMDEV_MOUSE_GUEST_CAN_ABSOLUTE | VMMDEV_MOUSE_NEW_PROTOCOL;
    mouse->x = 0;
    mouse->y = 0;
    rc = submit(VMMDEVREQ_SET_MOUSE_STATUS, sizeof(*mouse));
    if (rc < 0)
    {
        klogf(KLOG_WARN, "vboxmouse: set mouse status rc=%d, absolute pointer off", rc);
        return false;
    }
    ready = true;
    klogf(KLOG_OK, "vboxmouse: VMMDev io=0x%x, absolute pointer requested", io_base);
    return true;
}

void vbox_mouse_poll(void)
{
    if (!ready)
        return;
    if (__atomic_test_and_set(&busy, __ATOMIC_ACQUIRE))
        return;
    struct vmmdev_mouse_status *mouse = (struct vmmdev_mouse_status *)req_virt;
    mouse->features = 0;
    mouse->x = 0;
    mouse->y = 0;
    if (submit(VMMDEVREQ_GET_MOUSE_STATUS, sizeof(*mouse)) < 0)
    {
        __atomic_clear(&busy, __ATOMIC_RELEASE);
        return;
    }
    uint32_t features = ((volatile struct vmmdev_mouse_status *)req_virt)->features;
    int32_t x = ((volatile struct vmmdev_mouse_status *)req_virt)->x;
    int32_t y = ((volatile struct vmmdev_mouse_status *)req_virt)->y;
    __atomic_clear(&busy, __ATOMIC_RELEASE);
    bool wants = (features & VMMDEV_MOUSE_HOST_WANTS_ABSOLUTE) != 0;
    if (wants != host_wants_absolute)
    {
        host_wants_absolute = wants;
        mouse_set_absolute_mode(wants);
        klogf(KLOG_INFO, "vboxmouse: host %s absolute pointer",
              wants ? "wants" : "no longer wants");
    }
    if (wants)
        mouse_handle_absolute(x, y);
}
