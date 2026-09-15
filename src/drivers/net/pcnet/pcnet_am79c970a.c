#include "pcnet_am79c970a.h"
#include "drivers/interrupts/timer.h"
#include "drivers/pci/pci.h"
#include "kernel/diagnostics/klog.h"
#include "lib/string.h"
#include "mm/pmm.h"
#include "net/core/net_device.h"
#include <stddef.h>
#include <stdint.h>

#define PCNET_VENDOR_AMD 0x1022
#define PCNET_DEVICE_970A 0x2000
#define PCNET_DEVICE_2001 0x2001
#define PCNET_MAX_ADAPTERS 4
#define PCNET_RING_COUNT 16U
#define PCNET_RING_LOG2 4U
#define PCNET_BUFFER_SIZE 1548U
#define PCNET_INIT_TIMEOUT 1000000U
#define PCNET_START_TIMEOUT 100000U

/* 16-bit (word) access offsets, valid right after reset. */
#define PCNET16_RDP 0x10
#define PCNET16_RAP 0x12
#define PCNET16_RESET 0x14
/* 32-bit (dword) access offsets, valid after DWIO switch. */
#define PCNET32_RDP 0x10
#define PCNET32_RAP 0x14
#define PCNET32_RESET 0x18
#define PCNET32_BDP 0x1C

/* CSR0 bits. */
#define PCNET_CSR0_INIT 0x0001U
#define PCNET_CSR0_STRT 0x0002U
#define PCNET_CSR0_STOP 0x0004U
#define PCNET_CSR0_TDMD 0x0008U
#define PCNET_CSR0_TXON 0x0010U
#define PCNET_CSR0_RXON 0x0020U
#define PCNET_CSR0_IDON 0x0100U
#define PCNET_CSR0_INTS 0xFF00U

#define PCNET_CSR_INIT_LOW 1
#define PCNET_CSR_INIT_HIGH 2
#define PCNET_CSR_INT_MASK 3

/* BCR20: SSIZE32 (bit 8) + SWSTYLE 2 (PCnet-PCI II 32-bit). */
#define PCNET_BCR_SWSTYLE 20
#define PCNET_BCR_SWSTYLE_VALUE 0x0102U

/* Descriptor status bits (shared STP/ENP/OWN positions). */
#define PCNET_DESC_OWN 0x8000U
#define PCNET_DESC_ERR 0x4000U
#define PCNET_DESC_STP 0x0200U
#define PCNET_DESC_ENP 0x0100U
#define PCNET_TX_READY (PCNET_DESC_OWN|PCNET_DESC_STP|PCNET_DESC_ENP)

struct pcnet_init_block {
    uint16_t mode;
    uint16_t rlen_tlen;
    uint8_t padr[6];
    uint16_t reserved;
    uint32_t ladrf[2];
    uint32_t rdra;
    uint32_t tdra;
} __attribute__((packed));

struct pcnet_rx_descriptor {
    uint32_t base;
    uint16_t buffer_length;
    uint16_t status;
    uint32_t message_length;
    uint32_t reserved;
} __attribute__((packed));

struct pcnet_tx_descriptor {
    uint32_t base;
    uint16_t length;
    uint16_t status;
    uint32_t misc;
    uint32_t reserved;
} __attribute__((packed));

_Static_assert(sizeof(struct pcnet_init_block)==28,
               "PCnet init block ABI must be 28 bytes");
_Static_assert(sizeof(struct pcnet_rx_descriptor)==16,
               "PCnet RX descriptor ABI must be 16 bytes");
_Static_assert(sizeof(struct pcnet_tx_descriptor)==16,
               "PCnet TX descriptor ABI must be 16 bytes");
_Static_assert((PCNET_RING_COUNT*16U)%16U==0,
               "PCnet descriptor ring must be 16-byte aligned");

struct pcnet_device {
    uint16_t io_base;
    struct pci_device_info pci;
    struct net_device net;
    struct pcnet_init_block *init_block;
    uint64_t init_block_physical;
    volatile struct pcnet_rx_descriptor *rx_ring;
    volatile struct pcnet_tx_descriptor *tx_ring;
    uint64_t rx_ring_physical;
    uint64_t tx_ring_physical;
    uint64_t rx_buffer_physical[PCNET_RING_COUNT];
    uint64_t tx_buffer_physical[PCNET_RING_COUNT];
    uint8_t *rx_buffers[PCNET_RING_COUNT];
    uint8_t *tx_buffers[PCNET_RING_COUNT];
    uint16_t rx_next;
    uint16_t tx_next;
    volatile bool tx_locked;
    bool initialized;
};

static struct pcnet_device pcnet_adapters[PCNET_MAX_ADAPTERS];

struct pcnet_discovery {
    struct pci_device_info pci[PCNET_MAX_ADAPTERS];
    uint32_t count;
};

static inline uint8_t port_inb(uint16_t port){
    uint8_t value;
    __asm__ volatile("inb %1,%0":"=a"(value):"Nd"(port));
    return value;
}

static inline void port_outb(uint16_t port, uint8_t value){
    __asm__ volatile("outb %0,%1"::"a"(value),"Nd"(port));
}

static inline uint16_t port_inw(uint16_t port){
    uint16_t value;
    __asm__ volatile("inw %1,%0":"=a"(value):"Nd"(port));
    return value;
}

static inline void port_outw(uint16_t port, uint16_t value){
    __asm__ volatile("outw %0,%1"::"a"(value),"Nd"(port));
}

static inline uint32_t port_inl(uint16_t port){
    uint32_t value;
    __asm__ volatile("inl %1,%0":"=a"(value):"Nd"(port));
    return value;
}

static inline void port_outl(uint16_t port, uint32_t value){
    __asm__ volatile("outl %0,%1"::"a"(value),"Nd"(port));
}

static uint16_t pcnet_csr_read(struct pcnet_device *device, uint16_t csr){
    port_outl((uint16_t)(device->io_base+PCNET32_RAP),csr);
    return (uint16_t)(port_inl((uint16_t)(device->io_base+PCNET32_RDP))
                      &0xFFFFU);
}

static void pcnet_csr_write(struct pcnet_device *device, uint16_t csr,
                            uint16_t value){
    port_outl((uint16_t)(device->io_base+PCNET32_RAP),csr);
    port_outl((uint16_t)(device->io_base+PCNET32_RDP),value);
}

static uint16_t pcnet_bcr_read(struct pcnet_device *device, uint16_t bcr){
    port_outl((uint16_t)(device->io_base+PCNET32_RAP),bcr);
    return (uint16_t)(port_inl((uint16_t)(device->io_base+PCNET32_BDP))
                      &0xFFFFU);
}

static void pcnet_bcr_write(struct pcnet_device *device, uint16_t bcr,
                            uint16_t value){
    port_outl((uint16_t)(device->io_base+PCNET32_RAP),bcr);
    port_outl((uint16_t)(device->io_base+PCNET32_BDP),value);
}

static uint16_t pcnet_length_field(uint32_t length){
    return (uint16_t)(0xF000U|((0U-length)&0x0FFFU));
}

static void pcnet_release_dma(struct pcnet_device *device){
    for(uint32_t index=0;index<PCNET_RING_COUNT;index++){
        if(device->rx_buffer_physical[index])
            pmm_free_page(device->rx_buffer_physical[index]);
        if(device->tx_buffer_physical[index])
            pmm_free_page(device->tx_buffer_physical[index]);
    }
    if(device->rx_ring_physical) pmm_free_page(device->rx_ring_physical);
    if(device->tx_ring_physical) pmm_free_page(device->tx_ring_physical);
    if(device->init_block_physical)
        pmm_free_page(device->init_block_physical);
    device->rx_ring_physical=0;
    device->tx_ring_physical=0;
    device->init_block_physical=0;
}

static bool pcnet_allocate_dma(struct pcnet_device *device){
    device->init_block_physical=pmm_allocate_page();
    device->rx_ring_physical=pmm_allocate_page();
    device->tx_ring_physical=pmm_allocate_page();
    if(!device->init_block_physical || !device->rx_ring_physical
       || !device->tx_ring_physical){
        pcnet_release_dma(device);
        return false;
    }
    /* The 32-bit init block and descriptors cannot address above 4 GiB. */
    if((device->init_block_physical>>32)
       || (device->rx_ring_physical>>32)
       || (device->tx_ring_physical>>32)){
        pcnet_release_dma(device);
        return false;
    }
    device->init_block=pmm_physical_to_virtual(device->init_block_physical);
    device->rx_ring=pmm_physical_to_virtual(device->rx_ring_physical);
    device->tx_ring=pmm_physical_to_virtual(device->tx_ring_physical);
    for(uint32_t index=0;index<PCNET_RING_COUNT;index++){
        device->rx_buffer_physical[index]=pmm_allocate_page();
        device->tx_buffer_physical[index]=pmm_allocate_page();
        if(!device->rx_buffer_physical[index]
           || !device->tx_buffer_physical[index]
           || (device->rx_buffer_physical[index]>>32)
           || (device->tx_buffer_physical[index]>>32)){
            pcnet_release_dma(device);
            return false;
        }
        device->rx_buffers[index]=pmm_physical_to_virtual(
            device->rx_buffer_physical[index]);
        device->tx_buffers[index]=pmm_physical_to_virtual(
            device->tx_buffer_physical[index]);
    }
    return true;
}

/* Reset covers both DWIO states and leaves the card in 16-bit mode so the
   APROM bytes stay readable; the caller then switches to 32-bit I/O. */
static void pcnet_soft_reset(struct pcnet_device *device){
    (void)port_inl((uint16_t)(device->io_base+PCNET32_RESET));
    (void)port_inw((uint16_t)(device->io_base+PCNET16_RESET));
    timer_sleep(1);
}

static bool pcnet_enter_dword_mode(struct pcnet_device *device){
    /* After reset RAP points at CSR0, so this harmless write selects DWIO. */
    port_outl((uint16_t)(device->io_base+PCNET32_RDP),0);
    for(uint32_t attempt=0;attempt<PCNET_INIT_TIMEOUT;attempt++){
        uint16_t csr0=pcnet_csr_read(device,0);
        if((csr0&PCNET_CSR0_STOP)
           && !(csr0&(PCNET_CSR0_INIT|PCNET_CSR0_STRT))) return true;
        __asm__ volatile("pause");
    }
    return false;
}

static void pcnet_setup_rings(struct pcnet_device *device){
    uint16_t rx_bcnt=pcnet_length_field(PCNET_BUFFER_SIZE);
    for(uint32_t index=0;index<PCNET_RING_COUNT;index++){
        device->rx_ring[index].base=
            (uint32_t)device->rx_buffer_physical[index];
        device->rx_ring[index].buffer_length=rx_bcnt;
        device->rx_ring[index].status=PCNET_DESC_OWN;
        device->rx_ring[index].message_length=0;
        device->rx_ring[index].reserved=0;
        device->tx_ring[index].base=
            (uint32_t)device->tx_buffer_physical[index];
        device->tx_ring[index].length=0;
        device->tx_ring[index].status=0;
        device->tx_ring[index].misc=0;
        device->tx_ring[index].reserved=0;
    }
    __atomic_thread_fence(__ATOMIC_RELEASE);
    device->rx_next=0;
    device->tx_next=0;
}

static void pcnet_setup_init_block(struct pcnet_device *device){
    memset(device->init_block,0,sizeof(*device->init_block));
    device->init_block->mode=0x0000;
    device->init_block->rlen_tlen=(uint16_t)((PCNET_RING_LOG2<<12)
                                            |(PCNET_RING_LOG2<<4));
    memcpy(device->init_block->padr,device->net.mac,6);
    device->init_block->ladrf[0]=0;
    device->init_block->ladrf[1]=0;
    device->init_block->rdra=(uint32_t)device->rx_ring_physical;
    device->init_block->tdra=(uint32_t)device->tx_ring_physical;
    __atomic_thread_fence(__ATOMIC_RELEASE);
}

static bool pcnet_start(struct pcnet_device *device){
    pcnet_csr_write(device,PCNET_CSR_INIT_LOW,
                    (uint16_t)(device->init_block_physical&0xFFFFU));
    pcnet_csr_write(device,PCNET_CSR_INIT_HIGH,
                    (uint16_t)((device->init_block_physical>>16)&0xFFFFU));
    pcnet_csr_write(device,PCNET_CSR_INT_MASK,0x0000);
    pcnet_csr_write(device,0,PCNET_CSR0_INIT);
    bool done=false;
    for(uint32_t attempt=0;attempt<PCNET_INIT_TIMEOUT;attempt++){
        if(pcnet_csr_read(device,0)&PCNET_CSR0_IDON){
            done=true;
            break;
        }
        __asm__ volatile("pause");
    }
    if(!done) return false;
    /* Acknowledge IDON, then START the controller. */
    pcnet_csr_write(device,0,PCNET_CSR0_IDON);
    pcnet_csr_write(device,0,PCNET_CSR0_STRT);
    for(uint32_t attempt=0;attempt<PCNET_START_TIMEOUT;attempt++){
        uint16_t csr0=pcnet_csr_read(device,0);
        if((csr0&(PCNET_CSR0_RXON|PCNET_CSR0_TXON))
           ==(PCNET_CSR0_RXON|PCNET_CSR0_TXON)) return true;
        __asm__ volatile("pause");
    }
    return false;
}

static bool pcnet_transmit(void *context, const uint8_t *frame,
                           uint16_t length){
    struct pcnet_device *device=context;
    if(!device || !device->initialized || !frame) return false;
    if(length<14 || length>NET_ETHERNET_MAX_FRAME_SIZE) return false;
    if(__atomic_test_and_set(&device->tx_locked,__ATOMIC_ACQUIRE))
        return false;
    uint16_t index=device->tx_next;
    volatile struct pcnet_tx_descriptor *descriptor=&device->tx_ring[index];
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    if(descriptor->status&PCNET_DESC_OWN){
        __atomic_clear(&device->tx_locked,__ATOMIC_RELEASE);
        return false;
    }
    /* The controller cannot send runts; pad short frames in software. */
    uint32_t wire_length=length<60 ? 60 : length;
    memcpy(device->tx_buffers[index],frame,length);
    if(wire_length>length)
        memset(device->tx_buffers[index]+length,0,wire_length-length);
    descriptor->misc=0;
    descriptor->reserved=0;
    descriptor->length=pcnet_length_field(wire_length);
    __atomic_thread_fence(__ATOMIC_RELEASE);
    descriptor->status=PCNET_TX_READY;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    device->tx_next=(uint16_t)((index+1)%PCNET_RING_COUNT);
    /* Demand transmission without waiting for the poll timer. */
    pcnet_csr_write(device,0,PCNET_CSR0_TDMD);
    __atomic_clear(&device->tx_locked,__ATOMIC_RELEASE);
    return true;
}

static void pcnet_poll(void *context, uint32_t budget){
    struct pcnet_device *device=context;
    if(!device || !device->initialized) return;
    uint16_t csr0=pcnet_csr_read(device,0);
    if(csr0&PCNET_CSR0_INTS) pcnet_csr_write(device,0,csr0&PCNET_CSR0_INTS);
    for(uint32_t completed=0;completed<budget;completed++){
        uint16_t index=device->rx_next;
        volatile struct pcnet_rx_descriptor *descriptor=
            &device->rx_ring[index];
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if(descriptor->status&PCNET_DESC_OWN) break;
        uint16_t status=descriptor->status;
        uint32_t raw_length=descriptor->message_length&0x0FFFU;
        /* The byte count includes the 4-byte FCS trailer. */
        uint32_t frame_length=raw_length>=4 ? raw_length-4 : 0;
        if(!(status&PCNET_DESC_ERR)
           && (status&(PCNET_DESC_STP|PCNET_DESC_ENP))
              ==(PCNET_DESC_STP|PCNET_DESC_ENP)
           && frame_length>=14
           && frame_length<=NET_ETHERNET_MAX_FRAME_SIZE){
            (void)net_device_receive(&device->net,device->rx_buffers[index],
                                     (uint16_t)frame_length);
        } else {
            device->net.stats.rx_errors++;
        }
        descriptor->message_length=0;
        descriptor->buffer_length=pcnet_length_field(PCNET_BUFFER_SIZE);
        __atomic_thread_fence(__ATOMIC_RELEASE);
        descriptor->status=PCNET_DESC_OWN;
        __atomic_thread_fence(__ATOMIC_RELEASE);
        device->rx_next=(uint16_t)((index+1)%PCNET_RING_COUNT);
    }
}

static bool pcnet_link_up(void *context){
    struct pcnet_device *device=context;
    if(!device || !device->initialized) return false;
    uint16_t csr0=pcnet_csr_read(device,0);
    return (csr0&(PCNET_CSR0_RXON|PCNET_CSR0_TXON))
        ==(PCNET_CSR0_RXON|PCNET_CSR0_TXON);
}

static const struct net_device_ops pcnet_ops={
    .transmit=pcnet_transmit,
    .poll=pcnet_poll,
    .link_up=pcnet_link_up
};

static void pcnet_find_adapters(const struct pci_device_info *device,
                                void *context){
    struct pcnet_discovery *found=context;
    if(device->vendor_id!=PCNET_VENDOR_AMD) return;
    if(found->count>=PCNET_MAX_ADAPTERS) return;
    if(device->device_id!=PCNET_DEVICE_970A
       && device->device_id!=PCNET_DEVICE_2001) return;
    found->pci[found->count]=*device;
    found->count++;
}

static bool pcnet_init_one(struct pcnet_device *device,
                           const struct pci_device_info *pci){
    memset(device,0,sizeof(*device));
    device->pci=*pci;

    uint32_t bar0=pci_read_config32(pci->bus,pci->slot,pci->function,0x10);
    if(!bar0 || bar0==0xFFFFFFFFU || !(bar0&1U)){
        klog(KLOG_ERROR,"pcnet: BAR0 is not an I/O BAR");
        return false;
    }
    if(!pci_update_command(pci,
                           PCI_COMMAND_IO_SPACE|PCI_COMMAND_BUS_MASTER,0)){
        klog(KLOG_ERROR,"pcnet: failed to enable PCI I/O and bus mastering");
        return false;
    }
    uint64_t bar_address=pci_read_bar(pci->bus,pci->slot,pci->function,0);
    if(!bar_address || bar_address>0xFFFFU){
        klog(KLOG_ERROR,"pcnet: invalid I/O base from BAR0");
        return false;
    }
    device->io_base=(uint16_t)bar_address;

    pcnet_soft_reset(device);

    /* APROM bytes are readable while the card is in 16-bit reset state. */
    uint8_t mac[6];
    for(uint32_t index=0;index<6;index++)
        mac[index]=port_inb((uint16_t)(device->io_base+index));
    bool all_zero=true;
    bool all_ff=true;
    for(uint8_t index=0;index<6;index++){
        if(mac[index]) all_zero=false;
        if(mac[index]!=0xFF) all_ff=false;
    }
    if(all_zero || all_ff || (mac[0]&1U)){
        klog(KLOG_ERROR,"pcnet: invalid MAC address in APROM");
        return false;
    }
    memcpy(device->net.mac,mac,6);

    if(!pcnet_enter_dword_mode(device)){
        klog(KLOG_ERROR,"pcnet: DWIO switch timed out");
        return false;
    }

    /* Select the 32-bit PCnet-PCI II software style. */
    pcnet_bcr_write(device,PCNET_BCR_SWSTYLE,PCNET_BCR_SWSTYLE_VALUE);
    if((pcnet_bcr_read(device,PCNET_BCR_SWSTYLE)&0x01FFU)
       !=PCNET_BCR_SWSTYLE_VALUE){
        klog(KLOG_ERROR,"pcnet: failed to select 32-bit software style");
        return false;
    }

    if(!pcnet_allocate_dma(device)){
        klog(KLOG_ERROR,"pcnet: cannot allocate bounded DMA rings");
        return false;
    }
    pcnet_setup_rings(device);
    pcnet_setup_init_block(device);
    if(!pcnet_start(device)){
        klog(KLOG_ERROR,"pcnet: initialization or start timed out");
        pcnet_release_dma(device);
        return false;
    }

    uint32_t if_index=net_device_count();
    if(if_index>9) if_index=9;
    device->net.name[0]='e';
    device->net.name[1]='t';
    device->net.name[2]='h';
    device->net.name[3]=(char)('0'+if_index);
    device->net.name[4]='\0';
    device->net.mtu=NET_ETHERNET_MTU;
    device->net.ops=&pcnet_ops;
    device->net.driver_context=device;
    device->initialized=true;
    device->net.cached_link_up=pcnet_link_up(device);
    if(!net_device_register(&device->net)){
        device->initialized=false;
        pcnet_csr_write(device,0,PCNET_CSR0_STOP);
        pcnet_release_dma(device);
        return false;
    }
    klogf(KLOG_OK,
          "pcnet: %s Am79C970A %02x:%02x:%02x:%02x:%02x:%02x io=0x%x link=%s polling",
          device->net.name,
          device->net.mac[0],device->net.mac[1],device->net.mac[2],
          device->net.mac[3],device->net.mac[4],device->net.mac[5],
          device->io_base,
          device->net.cached_link_up ? "up" : "down");
    return true;
}

bool pcnet_am79c970a_init(void){
    struct pcnet_discovery found;
    memset(&found,0,sizeof(found));
    pci_enumerate(pcnet_find_adapters,&found);
    if(!found.count) return false;

    bool ready=false;
    uint32_t index=0;
    for(uint32_t candidate=0;candidate<found.count;candidate++){
        if(pcnet_init_one(&pcnet_adapters[index],&found.pci[candidate])){
            ready=true;
            index++;
        }
    }
    return ready;
}
