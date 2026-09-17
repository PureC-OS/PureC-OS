#include "vbe.h"
#include "gop.h"
#include "../pci/pci.h"
#include "../../arch/x86_64/mmio.h"
#include "../../mm/vmm.h"
#include "../../kernel/diagnostics/klog.h"

#define VBE_INDEX_PORT 0x1CE
#define VBE_VALUE_PORT 0x1CF

#define VBE_IDX_ID        0x00
#define VBE_IDX_XRES      0x01
#define VBE_IDX_YRES      0x02
#define VBE_IDX_BPP       0x03
#define VBE_IDX_ENABLE    0x04
#define VBE_IDX_BANK      0x05
#define VBE_IDX_VIDEOMEM  0x06
#define VBE_IDX_VIRTW     0x07
#define VBE_IDX_VIRTH     0x08
#define VBE_IDX_XOFF      0x09
#define VBE_IDX_YOFF      0x0A

#define VBE_ID0 0xB0C0
#define VBE_ID5 0xB0C5

#define VBE_ENABLE_ON  0x01
#define VBE_ENABLE_LFB 0x40

static inline void outw(uint16_t port, uint16_t value){
    __asm__ volatile("outw %0,%1"::"a"(value),"Nd"(port));
}

static inline uint16_t inw(uint16_t port){
    uint16_t value;
    __asm__ volatile("inw %1,%0":"=a"(value):"Nd"(port));
    return value;
}

static void vbe_write(uint16_t index, uint16_t value){
    outw(VBE_INDEX_PORT, index);
    outw(VBE_VALUE_PORT, value);
}

static uint16_t vbe_read(uint16_t index){
    outw(VBE_INDEX_PORT, index);
    return inw(VBE_VALUE_PORT);
}

bool vbe_is_available(void){
    vbe_write(VBE_IDX_ID, VBE_ID5);
    uint16_t id = vbe_read(VBE_IDX_ID);
    return id >= VBE_ID0 && id <= VBE_ID5;
}

uint64_t vbe_video_memory_bytes(void){
    if(!vbe_is_available()) return 0;
    return (uint64_t)vbe_read(VBE_IDX_VIDEOMEM) * 65536ULL;
}

struct vga_search {
    uint64_t bar0;
    bool found;
};

static void vga_visitor(const struct pci_device_info *device, void *context){
    struct vga_search *search = (struct vga_search *)context;
    if(!device || !search || search->found) return;
    if(device->class_code != 0x03) return;
    uint32_t raw = pci_read_config32(device->bus, device->slot,
                                     device->function, 0x10);
    if(raw == 0 || raw == 0xFFFFFFFF || (raw & 0x01)) return;
    uint64_t address = pci_read_bar(device->bus, device->slot,
                                    device->function, 0);
    if(!address) return;
    search->bar0 = address;
    search->found = true;
}

bool vbe_framebuffer_phys(uint64_t *physical_out){
    if(!physical_out) return false;
    struct vga_search search = {0, false};
    pci_enumerate(vga_visitor, &search);
    if(search.found){
        *physical_out = search.bar0;
        return true;
    }
    if(!gop_is_available()) return false;
    uint64_t virt = (uint64_t)(uintptr_t)gop_get_address();
    uint64_t phys = vmm_translate(vmm_kernel_address_space(), virt);
    if(!phys) return false;
    *physical_out = phys & ~0xFFFULL;
    return true;
}

bool vbe_set_mode(uint32_t width, uint32_t height, uint8_t bpp){
    if(!vbe_is_available()) return false;
    if(width < 640 || width > 7680 || height < 400 || height > 4320)
        return false;
    if(bpp != 16 && bpp != 24 && bpp != 32) return false;
    uint64_t need = (uint64_t)width * (uint64_t)height * (bpp / 8U);
    uint64_t total = vbe_video_memory_bytes();
    if(!total || need > total || need > VBE_MAX_FRAMEBUFFER_BYTES)
        return false;
    vbe_write(VBE_IDX_ENABLE, 0);
    vbe_write(VBE_IDX_BANK, 0);
    vbe_write(VBE_IDX_XRES, (uint16_t)width);
    vbe_write(VBE_IDX_YRES, (uint16_t)height);
    vbe_write(VBE_IDX_BPP, bpp);
    vbe_write(VBE_IDX_VIRTW, (uint16_t)width);
    vbe_write(VBE_IDX_VIRTH, (uint16_t)height);
    vbe_write(VBE_IDX_XOFF, 0);
    vbe_write(VBE_IDX_YOFF, 0);
    vbe_write(VBE_IDX_ENABLE, VBE_ENABLE_ON | VBE_ENABLE_LFB);
    if((uint32_t)vbe_read(VBE_IDX_XRES) != width
       || (uint32_t)vbe_read(VBE_IDX_YRES) != height)
        return false;
    return true;
}

volatile void *vbe_map_framebuffer(uint64_t physical, uint64_t size){
    if(!physical || !size || size > VBE_MAX_FRAMEBUFFER_BYTES) return NULL;
    if(!mmio_is_ready()) return NULL;
    return mmio_map(physical, size);
}
