#include "storage_probe.h"

#include "../pci/pci.h"
#include "../../kernel/diagnostics/klog.h"
#include "../../lib/string.h"

#define STORAGE_CONTROLLER_LIMIT 8
#define PCI_CLASS_MASS_STORAGE   0x01
#define PCI_SUBCLASS_SATA        0x06
#define PCI_SUBCLASS_NVM         0x08
#define PCI_INTERFACE_AHCI       0x01
#define PCI_INTERFACE_NVME_IO    0x02
#define PCI_INTERFACE_NVME_ADMIN 0x03
#define PCI_CLASS_SERIAL_BUS     0x0C
#define PCI_SUBCLASS_USB         0x03
#define PCI_INTERFACE_XHCI       0x30
#define PCI_INTERFACE_EHCI       0x20

static struct storage_controller_info controllers[STORAGE_CONTROLLER_LIMIT];
static uint8_t controller_count;
static uint8_t ahci_count;
static uint8_t nvme_count;
static uint8_t xhci_count;
static uint8_t ehci_count;
static bool probe_complete;

static void set_controller_name(struct storage_controller_info *controller,
                                const char *prefix, uint8_t index){
    memset(controller->name,0,sizeof(controller->name));
    uint8_t position=0;
    while(prefix[position] && position+2<sizeof(controller->name)){
        controller->name[position]=prefix[position];
        position++;
    }
    controller->name[position++]=(char)('0'+index);
    controller->name[position]='\0';
}

static void inspect_pci_device(const struct pci_device_info *device, void *context){
    (void)context;
    // логируем все PCI устройства для диагностики "невидимых" USB (UHCI/OHCI)
    klogf(KLOG_DEBUG,"pci scan: %02x:%02x.%u vend=%04x dev=%04x class=%02x sub=%02x prog=%02x rev=%02x",
          device->bus,device->slot,device->function,device->vendor_id,device->device_id,
          device->class_code,device->subclass,device->programming_interface,device->revision);
    if(device->class_code==PCI_CLASS_SERIAL_BUS && device->subclass==PCI_SUBCLASS_USB){
        klogf(KLOG_INFO,"pci usb: %02x:%02x.%u id %04x:%04x class %02x/%02x progIF 0x%02x -> %s",
              device->bus,device->slot,device->function,device->vendor_id,device->device_id,
              device->class_code,device->subclass,device->programming_interface,
              device->programming_interface==0x00?"UHCI":
              device->programming_interface==0x10?"OHCI":
              device->programming_interface==0x20?"EHCI":
              device->programming_interface==0x30?"xHCI":"UNKNOWN");
        if(device->programming_interface!=PCI_INTERFACE_XHCI && device->programming_interface!=PCI_INTERFACE_EHCI){
            klogf(KLOG_WARN,"pci usb: игнорируем контроллер progIF 0x%02x - поддерживаются только xHCI (0x30) и EHCI (0x20); устройства на этом контроллере будут невидимы!",device->programming_interface);
        }
    }
    if(controller_count>=STORAGE_CONTROLLER_LIMIT){
        klogf(KLOG_WARN,"pci: controller limit %u reached, skip %02x:%02x.%u",STORAGE_CONTROLLER_LIMIT,device->bus,device->slot,device->function);
        return;
    }

    uint8_t type=0;
    uint8_t bar_index=0;
    if(device->class_code==PCI_CLASS_MASS_STORAGE
       && device->subclass==PCI_SUBCLASS_SATA
       && device->programming_interface==PCI_INTERFACE_AHCI){
        type=STORAGE_CONTROLLER_AHCI;
        bar_index=5;
    } else if(device->class_code==PCI_CLASS_MASS_STORAGE
              && device->subclass==PCI_SUBCLASS_NVM
              && (device->programming_interface==PCI_INTERFACE_NVME_IO
                  || device->programming_interface==PCI_INTERFACE_NVME_ADMIN)){
        type=STORAGE_CONTROLLER_NVME;
    } else if(device->class_code==PCI_CLASS_SERIAL_BUS
              && device->subclass==PCI_SUBCLASS_USB
              && (device->programming_interface==PCI_INTERFACE_XHCI
                  || device->programming_interface==PCI_INTERFACE_EHCI)){
        type=device->programming_interface==PCI_INTERFACE_XHCI
            ? STORAGE_CONTROLLER_XHCI : STORAGE_CONTROLLER_EHCI;
    } else {
        return;
    }

    struct storage_controller_info *controller=&controllers[controller_count];
    memset(controller,0,sizeof(*controller));
    controller->register_base=pci_read_bar(device->bus,device->slot,
                                           device->function,bar_index);
    uint32_t bar_low=pci_read_config32(device->bus,device->slot,device->function,0x10+bar_index*4);
    uint32_t bar_high=0;
    if(bar_index<5 && (bar_low&0x06)==0x04){
        bar_high=pci_read_config32(device->bus,device->slot,device->function,0x14+bar_index*4);
    }
    controller->vendor_id=device->vendor_id;
    controller->device_id=device->device_id;
    controller->bus=device->bus;
    controller->slot=device->slot;
    controller->function=device->function;
    controller->type=type;
    controller->programming_interface=device->programming_interface;
    if(type==STORAGE_CONTROLLER_AHCI){
        set_controller_name(controller,"ahci",ahci_count++);
    } else if(type==STORAGE_CONTROLLER_NVME){
        set_controller_name(controller,"nvme",nvme_count++);
    } else if(type==STORAGE_CONTROLLER_XHCI){
        set_controller_name(controller,"xhci",xhci_count++);
    } else {
        set_controller_name(controller,"ehci",ehci_count++);
    }
    klogf(KLOG_OK,"pci: found %s pci %02x:%02x.%u BAR%u low=0x%08x high=0x%08x phys=0x%llx id=%04x:%04x",
          controller->name,controller->bus,controller->slot,controller->function,bar_index,bar_low,bar_high,controller->register_base,
          controller->vendor_id,controller->device_id);
    if(controller->register_base==0){
        klogf(KLOG_ERROR,"pci: %s BAR is zero! BIOS не назначил MMIO – проверь QEMU -device qemu-xhci, host controller may be disabled",controller->name);
    }
    controller_count++;
}

void storage_probe_init(void){
    if(probe_complete){
        return;
    }
    probe_complete=true;
    // Детальное PCI сканирование пишем только в ring+dmesg, без перерисовки GOP на реальном железе
    bool was_screen=klog_is_screen_enabled();
    klog_set_screen_enabled(false);
    klog(KLOG_INFO,"storage_probe: enumerating PCI bus (256*32*8)");
    pci_enumerate(inspect_pci_device,0);
    klogf(KLOG_INFO,"storage_probe: done %u controllers (ahci=%u nvme=%u xhci=%u ehci=%u)",controller_count,ahci_count,nvme_count,xhci_count,ehci_count);
    klog_set_screen_enabled(was_screen);
    // Краткий итог - показываем на экране загрузки (1 строка, без мерцания)
    klogf(KLOG_INFO,"pci: %u storage/USB controller(s)",controller_count);
    if(controller_count==0){
        klog(KLOG_WARN,"storage_probe: no AHCI/NVMe/xHCI/EHCI found");
    }
    if(xhci_count==0 && ehci_count==0 && controller_count){
        klog(KLOG_WARN,"storage_probe: нет xHCI/EHCI – USB storage невозможен");
    }
}

uint32_t storage_controller_count(void){
    storage_probe_init();
    return controller_count;
}

int32_t storage_controller_list(struct storage_controller_info *output,
                                uint32_t capacity){
    if(!output || capacity==0 || capacity>0x7FFFFFFF) return -1;
    storage_probe_init();
    uint32_t count=controller_count;
    if(count>capacity) count=capacity;
    for(uint32_t index=0;index<count;index++) output[index]=controllers[index];
    return (int32_t)count;
}
