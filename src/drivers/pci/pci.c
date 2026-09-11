#include "pci.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC
#define PCI_VENDOR_NONE    0xFFFF
#define PCI_HEADER_MULTI   0x80
#define PCI_BAR_IO         0x01
#define PCI_BAR_MEMORY_64  0x04

static inline void outl(uint16_t port, uint32_t value){
    __asm__ volatile("outl %0,%1"::"a"(value),"Nd"(port));
}

static inline void outw(uint16_t port, uint16_t value){
    __asm__ volatile("outw %0,%1"::"a"(value),"Nd"(port));
}

static inline uint32_t inl(uint16_t port){
    uint32_t value;
    __asm__ volatile("inl %1,%0":"=a"(value):"Nd"(port));
    return value;
}

uint32_t pci_read_config32(uint8_t bus, uint8_t slot, uint8_t function,
                           uint8_t offset){
    uint32_t address=0x80000000U|((uint32_t)bus<<16)|((uint32_t)slot<<11)
        |((uint32_t)function<<8)|(offset&0xFC);
    outl(PCI_CONFIG_ADDRESS,address);
    return inl(PCI_CONFIG_DATA);
}

void pci_write_config32(uint8_t bus, uint8_t slot, uint8_t function,
                        uint8_t offset, uint32_t value){
    uint32_t address=0x80000000U|((uint32_t)bus<<16)|((uint32_t)slot<<11)
        |((uint32_t)function<<8)|(offset&0xFC);
    outl(PCI_CONFIG_ADDRESS,address);
    outl(PCI_CONFIG_DATA,value);
}

bool pci_update_command(const struct pci_device_info *device,
                        uint16_t set_bits, uint16_t clear_bits){
    if(!device) return false;
    uint32_t command_status=pci_read_config32(
        device->bus,device->slot,device->function,0x04);
    uint16_t command=(uint16_t)command_status;
    command=(uint16_t)((command|set_bits)&(uint16_t)~clear_bits);
    uint32_t address=0x80000000U|((uint32_t)device->bus<<16)
        |((uint32_t)device->slot<<11)|((uint32_t)device->function<<8)|0x04;
    outl(PCI_CONFIG_ADDRESS,address);
    outw(PCI_CONFIG_DATA,command);
    uint16_t verified=(uint16_t)pci_read_config32(
        device->bus,device->slot,device->function,0x04);
    return (verified&set_bits)==set_bits && (verified&clear_bits)==0;
}

uint64_t pci_read_bar(uint8_t bus, uint8_t slot, uint8_t function,
                      uint8_t bar_index){
    if(bar_index>=6) return 0;
    uint8_t offset=(uint8_t)(0x10+bar_index*4);
    uint32_t low=pci_read_config32(bus,slot,function,offset);
    if(low==0 || low==0xFFFFFFFF) return 0;
    if(low&PCI_BAR_IO) return low&~0x3U;

    uint64_t address=low&~0xFU;
    if((low&0x06)==PCI_BAR_MEMORY_64 && bar_index<5){
        uint32_t high=pci_read_config32(bus,slot,function,(uint8_t)(offset+4));
        if(high==0xFFFFFFFF) return 0;
        address|=(uint64_t)high<<32;
    }
    return address;
}

static void visit_function(uint8_t bus, uint8_t slot, uint8_t function,
                           pci_device_visitor visitor, void *context){
    uint32_t identity=pci_read_config32(bus,slot,function,0x00);
    if((uint16_t)identity==PCI_VENDOR_NONE) return;

    uint32_t class_register=pci_read_config32(bus,slot,function,0x08);
    struct pci_device_info device={
        .vendor_id=(uint16_t)identity,
        .device_id=(uint16_t)(identity>>16),
        .bus=bus,
        .slot=slot,
        .function=function,
        .class_code=(uint8_t)(class_register>>24),
        .subclass=(uint8_t)(class_register>>16),
        .programming_interface=(uint8_t)(class_register>>8),
        .revision=(uint8_t)class_register
    };
    visitor(&device,context);
}

void pci_enumerate(pci_device_visitor visitor, void *context){
    if(!visitor) return;
    for(uint16_t bus=0;bus<256;bus++){
        for(uint8_t slot=0;slot<32;slot++){
            uint32_t identity=pci_read_config32((uint8_t)bus,slot,0,0x00);
            if((uint16_t)identity==PCI_VENDOR_NONE) continue;

            visit_function((uint8_t)bus,slot,0,visitor,context);
            uint32_t header=pci_read_config32((uint8_t)bus,slot,0,0x0C);
            if(!((header>>16)&PCI_HEADER_MULTI)) continue;
            for(uint8_t function=1;function<8;function++){
                visit_function((uint8_t)bus,slot,function,visitor,context);
            }
        }
    }
}
