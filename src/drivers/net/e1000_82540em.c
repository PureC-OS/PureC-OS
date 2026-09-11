#include "e1000_82540em.h"
#include "../pci/pci.h"
#include "../../arch/x86_64/mmio.h"
#include "../../kernel/diagnostics/klog.h"
#include "../../lib/string.h"
#include "../../mm/pmm.h"
#include "../../net/core/net_device.h"
#include <stddef.h>
#include <stdint.h>

#define E1000_VENDOR_INTEL 0x8086
#define E1000_DEVICE_82540EM 0x100E
#define E1000_MMIO_SIZE 0x20000U
#define E1000_RING_COUNT 16U
#define E1000_DMA_BUFFER_SIZE 2048U
#define E1000_POLL_TIMEOUT 1000000U

#define E1000_REG_CTRL   0x0000
#define E1000_REG_STATUS 0x0008
#define E1000_REG_ICR    0x00C0
#define E1000_REG_IMC    0x00D8
#define E1000_REG_RCTL   0x0100
#define E1000_REG_TCTL   0x0400
#define E1000_REG_TIPG   0x0410
#define E1000_REG_RDBAL  0x2800
#define E1000_REG_RDBAH  0x2804
#define E1000_REG_RDLEN  0x2808
#define E1000_REG_RDH    0x2810
#define E1000_REG_RDT    0x2818
#define E1000_REG_TDBAL  0x3800
#define E1000_REG_TDBAH  0x3804
#define E1000_REG_TDLEN  0x3808
#define E1000_REG_TDH    0x3810
#define E1000_REG_TDT    0x3818
#define E1000_REG_MTA    0x5200
#define E1000_REG_RAL    0x5400
#define E1000_REG_RAH    0x5404

#define E1000_CTRL_SLU (1U << 6)
#define E1000_CTRL_RST (1U << 26)
#define E1000_STATUS_LU (1U << 1)
#define E1000_RCTL_EN (1U << 1)
#define E1000_RCTL_BAM (1U << 15)
#define E1000_RCTL_SECRC (1U << 26)
#define E1000_TCTL_EN (1U << 1)
#define E1000_TCTL_PSP (1U << 3)
#define E1000_RX_STATUS_DD (1U << 0)
#define E1000_RX_STATUS_EOP (1U << 1)
#define E1000_TX_CMD_EOP (1U << 0)
#define E1000_TX_CMD_IFCS (1U << 1)
#define E1000_TX_CMD_RS (1U << 3)
#define E1000_TX_STATUS_DD (1U << 0)

struct e1000_rx_descriptor {
    uint64_t address;
    uint16_t length;
    uint16_t checksum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
} __attribute__((packed));

struct e1000_tx_descriptor {
    uint64_t address;
    uint16_t length;
    uint8_t checksum_offset;
    uint8_t command;
    uint8_t status;
    uint8_t checksum_start;
    uint16_t special;
} __attribute__((packed));

_Static_assert(sizeof(struct e1000_rx_descriptor)==16,
               "e1000 RX descriptor ABI must be 16 bytes");
_Static_assert(sizeof(struct e1000_tx_descriptor)==16,
               "e1000 TX descriptor ABI must be 16 bytes");
_Static_assert((E1000_RING_COUNT*16U)%128U==0,
               "e1000 descriptor ring length must be 128-byte aligned");

struct e1000_device {
    volatile uint8_t *registers;
    struct pci_device_info pci;
    struct net_device net;
    volatile struct e1000_rx_descriptor *rx_ring;
    volatile struct e1000_tx_descriptor *tx_ring;
    uint64_t rx_ring_physical;
    uint64_t tx_ring_physical;
    uint64_t rx_buffer_physical[E1000_RING_COUNT];
    uint64_t tx_buffer_physical[E1000_RING_COUNT];
    uint8_t *rx_buffers[E1000_RING_COUNT];
    uint8_t *tx_buffers[E1000_RING_COUNT];
    uint16_t rx_next;
    uint16_t tx_next;
    volatile bool tx_locked;
    bool found;
    bool initialized;
};

static struct e1000_device adapter;

static uint32_t reg_read(uint32_t offset){
    return *(volatile uint32_t*)(adapter.registers+offset);
}

static void reg_write(uint32_t offset, uint32_t value){
    *(volatile uint32_t*)(adapter.registers+offset)=value;
}

static void split_address(uint64_t address, uint32_t low_register,
                          uint32_t high_register){
    reg_write(low_register,(uint32_t)address);
    reg_write(high_register,(uint32_t)(address>>32));
}

static void release_dma(void){
    for(uint32_t index=0;index<E1000_RING_COUNT;index++){
        if(adapter.rx_buffer_physical[index])
            pmm_free_page(adapter.rx_buffer_physical[index]);
        if(adapter.tx_buffer_physical[index])
            pmm_free_page(adapter.tx_buffer_physical[index]);
    }
    if(adapter.rx_ring_physical) pmm_free_page(adapter.rx_ring_physical);
    if(adapter.tx_ring_physical) pmm_free_page(adapter.tx_ring_physical);
    adapter.rx_ring_physical=0;
    adapter.tx_ring_physical=0;
}

static bool allocate_dma(void){
    adapter.rx_ring_physical=pmm_allocate_page();
    adapter.tx_ring_physical=pmm_allocate_page();
    if(!adapter.rx_ring_physical || !adapter.tx_ring_physical){
        release_dma();
        return false;
    }
    adapter.rx_ring=pmm_physical_to_virtual(adapter.rx_ring_physical);
    adapter.tx_ring=pmm_physical_to_virtual(adapter.tx_ring_physical);
    for(uint32_t index=0;index<E1000_RING_COUNT;index++){
        adapter.rx_buffer_physical[index]=pmm_allocate_page();
        adapter.tx_buffer_physical[index]=pmm_allocate_page();
        if(!adapter.rx_buffer_physical[index]
           || !adapter.tx_buffer_physical[index]){
            release_dma();
            return false;
        }
        adapter.rx_buffers[index]=pmm_physical_to_virtual(
            adapter.rx_buffer_physical[index]);
        adapter.tx_buffers[index]=pmm_physical_to_virtual(
            adapter.tx_buffer_physical[index]);
        adapter.rx_ring[index].address=adapter.rx_buffer_physical[index];
        adapter.tx_ring[index].address=adapter.tx_buffer_physical[index];
        adapter.tx_ring[index].status=E1000_TX_STATUS_DD;
    }
    return true;
}

static bool read_mac(uint8_t mac[6]){
    uint32_t low=reg_read(E1000_REG_RAL);
    uint32_t high=reg_read(E1000_REG_RAH);
    mac[0]=(uint8_t)low;
    mac[1]=(uint8_t)(low>>8);
    mac[2]=(uint8_t)(low>>16);
    mac[3]=(uint8_t)(low>>24);
    mac[4]=(uint8_t)high;
    mac[5]=(uint8_t)(high>>8);
    bool all_zero=true;
    bool all_ff=true;
    for(uint8_t index=0;index<6;index++){
        if(mac[index]) all_zero=false;
        if(mac[index]!=0xFF) all_ff=false;
    }
    return !all_zero && !all_ff && !(mac[0]&1U);
}

static void program_receive_address(const uint8_t mac[6]){
    uint32_t low=(uint32_t)mac[0]|((uint32_t)mac[1]<<8)
        |((uint32_t)mac[2]<<16)|((uint32_t)mac[3]<<24);
    uint32_t high=(uint32_t)mac[4]|((uint32_t)mac[5]<<8)|(1U<<31);
    reg_write(E1000_REG_RAL,low);
    reg_write(E1000_REG_RAH,high);
}

static bool reset_controller(void){
    reg_write(E1000_REG_IMC,0xFFFFFFFFU);
    (void)reg_read(E1000_REG_ICR);
    reg_write(E1000_REG_CTRL,reg_read(E1000_REG_CTRL)|E1000_CTRL_RST);
    for(uint32_t attempt=0;attempt<E1000_POLL_TIMEOUT;attempt++){
        if(!(reg_read(E1000_REG_CTRL)&E1000_CTRL_RST)){
            reg_write(E1000_REG_IMC,0xFFFFFFFFU);
            (void)reg_read(E1000_REG_ICR);
            return true;
        }
        __asm__ volatile("pause");
    }
    return false;
}

static void initialize_receive(void){
    split_address(adapter.rx_ring_physical,E1000_REG_RDBAL,E1000_REG_RDBAH);
    reg_write(E1000_REG_RDLEN,
              E1000_RING_COUNT*sizeof(struct e1000_rx_descriptor));
    reg_write(E1000_REG_RDH,0);
    reg_write(E1000_REG_RDT,E1000_RING_COUNT-1);
    adapter.rx_next=0;
    reg_write(E1000_REG_RCTL,E1000_RCTL_EN|E1000_RCTL_BAM|E1000_RCTL_SECRC);
}

static void initialize_transmit(void){
    split_address(adapter.tx_ring_physical,E1000_REG_TDBAL,E1000_REG_TDBAH);
    reg_write(E1000_REG_TDLEN,
              E1000_RING_COUNT*sizeof(struct e1000_tx_descriptor));
    reg_write(E1000_REG_TDH,0);
    reg_write(E1000_REG_TDT,0);
    adapter.tx_next=0;
    reg_write(E1000_REG_TIPG,10U|(8U<<10)|(6U<<20));
    reg_write(E1000_REG_TCTL,E1000_TCTL_EN|E1000_TCTL_PSP
              |(15U<<4)|(64U<<12));
}

static bool e1000_transmit(void *context, const uint8_t *frame,
                           uint16_t length){
    struct e1000_device *device=context;
    if(!device || !device->initialized || length>E1000_DMA_BUFFER_SIZE)
        return false;
    if(__atomic_test_and_set(&device->tx_locked,__ATOMIC_ACQUIRE)) return false;
    uint16_t index=device->tx_next;
    volatile struct e1000_tx_descriptor *descriptor=&device->tx_ring[index];
    if(!(descriptor->status&E1000_TX_STATUS_DD)){
        __atomic_clear(&device->tx_locked,__ATOMIC_RELEASE);
        return false;
    }
    memcpy(device->tx_buffers[index],frame,length);
    descriptor->length=length;
    descriptor->checksum_offset=0;
    descriptor->checksum_start=0;
    descriptor->special=0;
    descriptor->status=0;
    descriptor->command=E1000_TX_CMD_EOP|E1000_TX_CMD_IFCS|E1000_TX_CMD_RS;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    device->tx_next=(uint16_t)((index+1)%E1000_RING_COUNT);
    reg_write(E1000_REG_TDT,device->tx_next);
    __atomic_clear(&device->tx_locked,__ATOMIC_RELEASE);
    return true;
}

static void e1000_poll(void *context, uint32_t budget){
    struct e1000_device *device=context;
    if(!device || !device->initialized) return;
    for(uint32_t completed=0;completed<budget;completed++){
        uint16_t index=device->rx_next;
        volatile struct e1000_rx_descriptor *descriptor=&device->rx_ring[index];
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if(!(descriptor->status&E1000_RX_STATUS_DD)) break;
        if(!descriptor->errors && (descriptor->status&E1000_RX_STATUS_EOP)
           && descriptor->length<=NET_ETHERNET_MAX_FRAME_SIZE){
            (void)net_device_receive(&device->net,device->rx_buffers[index],
                                     descriptor->length);
        } else {
            device->net.stats.rx_errors++;
        }
        descriptor->status=0;
        descriptor->errors=0;
        descriptor->length=0;
        __atomic_thread_fence(__ATOMIC_RELEASE);
        reg_write(E1000_REG_RDT,index);
        device->rx_next=(uint16_t)((index+1)%E1000_RING_COUNT);
    }
}

static bool e1000_link_up(void *context){
    struct e1000_device *device=context;
    return device && device->initialized
        && (reg_read(E1000_REG_STATUS)&E1000_STATUS_LU)!=0;
}

static const struct net_device_ops e1000_ops={
    .transmit=e1000_transmit,
    .poll=e1000_poll,
    .link_up=e1000_link_up
};

static void find_adapter(const struct pci_device_info *device, void *context){
    struct e1000_device *result=context;
    if(result->found || device->vendor_id!=E1000_VENDOR_INTEL
       || device->device_id!=E1000_DEVICE_82540EM) return;
    result->pci=*device;
    result->found=true;
}

bool e1000_82540em_init(void){
    memset(&adapter,0,sizeof(adapter));
    pci_enumerate(find_adapter,&adapter);
    if(!adapter.found) return false;

    uint32_t bar0=pci_read_config32(adapter.pci.bus,adapter.pci.slot,
                                    adapter.pci.function,0x10);
    if(!bar0 || bar0==0xFFFFFFFFU || (bar0&1U)){
        klog(KLOG_ERROR,"e1000: BAR0 is not a memory BAR");
        return false;
    }
    if(!pci_update_command(&adapter.pci,
                           PCI_COMMAND_MEMORY|PCI_COMMAND_BUS_MASTER,0)){
        klog(KLOG_ERROR,"e1000: failed to enable PCI memory and bus mastering");
        return false;
    }
    uint64_t bar_address=pci_read_bar(adapter.pci.bus,adapter.pci.slot,
                                      adapter.pci.function,0);
    adapter.registers=mmio_map(bar_address,E1000_MMIO_SIZE);
    if(!adapter.registers){
        klog(KLOG_ERROR,"e1000: cannot map 128 KiB MMIO BAR");
        return false;
    }
    if(!reset_controller()){
        klog(KLOG_ERROR,"e1000: controller reset timed out");
        return false;
    }
    if(!read_mac(adapter.net.mac)){
        klog(KLOG_ERROR,"e1000: invalid MAC address after EEPROM reload");
        return false;
    }
    program_receive_address(adapter.net.mac);
    if(!allocate_dma()){
        klog(KLOG_ERROR,"e1000: cannot allocate bounded DMA rings");
        return false;
    }

    for(uint32_t index=0;index<128;index++)
        reg_write(E1000_REG_MTA+index*4,0);
    initialize_receive();
    initialize_transmit();
    reg_write(E1000_REG_CTRL,reg_read(E1000_REG_CTRL)|E1000_CTRL_SLU);

    strncpy(adapter.net.name,"eth0",sizeof(adapter.net.name)-1);
    adapter.net.mtu=NET_ETHERNET_MTU;
    adapter.net.ops=&e1000_ops;
    adapter.net.driver_context=&adapter;
    adapter.initialized=true;
    adapter.net.cached_link_up=e1000_link_up(&adapter);
    if(!net_device_register(&adapter.net)){
        adapter.initialized=false;
        reg_write(E1000_REG_RCTL,0);
        reg_write(E1000_REG_TCTL,0);
        release_dma();
        return false;
    }
    klogf(KLOG_OK,
          "e1000: eth0 82540EM %02x:%02x:%02x:%02x:%02x:%02x link=%s polling",
          adapter.net.mac[0],adapter.net.mac[1],adapter.net.mac[2],
          adapter.net.mac[3],adapter.net.mac[4],adapter.net.mac[5],
          adapter.net.cached_link_up ? "up" : "down");
    return true;
}
