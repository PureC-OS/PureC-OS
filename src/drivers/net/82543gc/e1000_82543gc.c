#include "e1000_82543gc.h"
#include "arch/x86_64/mmio.h"
#include "drivers/interrupts/timer.h"
#include "drivers/pci/pci.h"
#include "kernel/diagnostics/klog.h"
#include "lib/string.h"
#include "mm/pmm.h"
#include "net/core/net_device.h"
#include <stddef.h>
#include <stdint.h>

#define E1000_GC_VENDOR_INTEL 0x8086
#define E1000_GC_DEVICE_82543GC_COPPER 0x1004
#define E1000_GC_DEVICE_82543GC_FIBER 0x1001
#define E1000_GC_DEVICE_82544GC_COPPER 0x1008
#define E1000_GC_MAX_ADAPTERS 4
#define E1000_GC_MMIO_SIZE 0x20000U
#define E1000_GC_RING_COUNT 16U
#define E1000_GC_DMA_BUFFER_SIZE 2048U
#define E1000_GC_POLL_TIMEOUT 1000000U
#define E1000_GC_MDIO_TIMEOUT 1024U
#define E1000_GC_AN_TIMEOUT_MS 3000U
#define E1000_GC_PHY_LINK_TIMEOUT_MS 500U

#define E1000_GC_REG_CTRL   0x0000
#define E1000_GC_REG_STATUS 0x0008
#define E1000_GC_REG_CTRL_EXT 0x0018
#define E1000_GC_REG_MDIC   0x0020
#define E1000_GC_REG_ICR    0x00C0
#define E1000_GC_REG_IMC    0x00D8
#define E1000_GC_REG_RCTL   0x0100
#define E1000_GC_REG_TCTL   0x0400
#define E1000_GC_REG_TIPG   0x0410
#define E1000_GC_REG_RDBAL  0x2800
#define E1000_GC_REG_RDBAH  0x2804
#define E1000_GC_REG_RDLEN  0x2808
#define E1000_GC_REG_RDH    0x2810
#define E1000_GC_REG_RDT    0x2818
#define E1000_GC_REG_TDBAL  0x3800
#define E1000_GC_REG_TDBAH  0x3804
#define E1000_GC_REG_TDLEN  0x3808
#define E1000_GC_REG_TDH    0x3810
#define E1000_GC_REG_TDT    0x3818
#define E1000_GC_REG_MTA    0x5200
#define E1000_GC_REG_RAL    0x5400
#define E1000_GC_REG_RAH    0x5404

#define E1000_GC_CTRL_FD (1U << 0)
#define E1000_GC_CTRL_SLU (1U << 6)
#define E1000_GC_CTRL_ILOS (1U << 7)
#define E1000_GC_CTRL_SPD_SEL 0x00000300U
#define E1000_GC_CTRL_SPD_100 0x00000100U
#define E1000_GC_CTRL_SPD_1000 0x00000200U
#define E1000_GC_CTRL_FRCSPD (1U << 11)
#define E1000_GC_CTRL_FRCDPX (1U << 12)
#define E1000_GC_CTRL_RST (1U << 26)
#define E1000_GC_STATUS_LU (1U << 1)

#define E1000_GC_CTRL_EXT_SDP4_DIR 0x00400000U
#define E1000_GC_CTRL_EXT_SDP4_DATA 0x00000010U

#define E1000_GC_MDIC_DATA_MASK 0x0000FFFFU
#define E1000_GC_MDIC_REG_SHIFT 16
#define E1000_GC_MDIC_PHY_SHIFT 21
#define E1000_GC_MDIC_OP_WRITE (1U << 26)
#define E1000_GC_MDIC_OP_READ (1U << 27)
#define E1000_GC_MDIC_READY (1U << 28)
#define E1000_GC_MDIC_ERROR (1U << 30)

#define E1000_GC_RCTL_EN (1U << 1)
#define E1000_GC_RCTL_BAM (1U << 15)
#define E1000_GC_RCTL_SECRC (1U << 26)
#define E1000_GC_TCTL_EN (1U << 1)
#define E1000_GC_TCTL_PSP (1U << 3)
#define E1000_GC_TCTL_CT_SHIFT 4
#define E1000_GC_TCTL_COLD_SHIFT 12
#define E1000_GC_TCTL_COLD 63U
#define E1000_GC_RX_STATUS_DD (1U << 0)
#define E1000_GC_RX_STATUS_EOP (1U << 1)
#define E1000_GC_TX_CMD_EOP (1U << 0)
#define E1000_GC_TX_CMD_IFCS (1U << 1)
#define E1000_GC_TX_CMD_RS (1U << 3)
#define E1000_GC_TX_STATUS_DD (1U << 0)

#define E1000_GC_MII_BMCR 0x00
#define E1000_GC_MII_BMSR 0x01
#define E1000_GC_MII_PHYSID1 0x02
#define E1000_GC_MII_PHYSID2 0x03
#define E1000_GC_MII_ANAR 0x04
#define E1000_GC_MII_1000T_CTRL 0x09
#define E1000_GC_BMCR_ANENABLE 0x1000U
#define E1000_GC_BMCR_ANRESTART 0x0200U
#define E1000_GC_BMSR_ANEGCOMPLETE 0x0020U
#define E1000_GC_ANAR_10H_10F_100H_100F 0x01E0U
#define E1000_GC_ANAR_PAUSE_SYM 0x0400U
#define E1000_GC_1000T_ADV_FULL 0x0200U

#define E1000_GC_M88_PSSR 0x11
#define E1000_GC_M88_PSSR_LINK 0x0400U
#define E1000_GC_M88_PSSR_RESOLVED 0x0800U
#define E1000_GC_M88_PSSR_DPLX 0x2000U
#define E1000_GC_M88_PSSR_SPEED_MASK 0xC000U
#define E1000_GC_M88_PSSR_10MBS 0x0000U
#define E1000_GC_M88_PSSR_100MBS 0x4000U
#define E1000_GC_M88_PSSR_1000MBS 0x8000U

struct e1000_gc_rx_descriptor {
    uint64_t address;
    uint16_t length;
    uint16_t checksum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
} __attribute__((packed));

struct e1000_gc_tx_descriptor {
    uint64_t address;
    uint16_t length;
    uint8_t checksum_offset;
    uint8_t command;
    uint8_t status;
    uint8_t checksum_start;
    uint16_t special;
} __attribute__((packed));

_Static_assert(sizeof(struct e1000_gc_rx_descriptor)==16,
               "e1000 82543GC RX descriptor ABI must be 16 bytes");
_Static_assert(sizeof(struct e1000_gc_tx_descriptor)==16,
               "e1000 82543GC TX descriptor ABI must be 16 bytes");
_Static_assert((E1000_GC_RING_COUNT*16U)%128U==0,
               "e1000 82543GC descriptor ring length must be 128-byte aligned");

struct e1000_gc_device {
    volatile uint8_t *registers;
    struct pci_device_info pci;
    struct net_device net;
    volatile struct e1000_gc_rx_descriptor *rx_ring;
    volatile struct e1000_gc_tx_descriptor *tx_ring;
    uint64_t rx_ring_physical;
    uint64_t tx_ring_physical;
    uint64_t rx_buffer_physical[E1000_GC_RING_COUNT];
    uint64_t tx_buffer_physical[E1000_GC_RING_COUNT];
    uint8_t *rx_buffers[E1000_GC_RING_COUNT];
    uint8_t *tx_buffers[E1000_GC_RING_COUNT];
    uint16_t rx_next;
    uint16_t tx_next;
    volatile bool tx_locked;
    uint8_t phy_addr;
    uint16_t link_speed_mbps;
    bool full_duplex;
    bool initialized;
};

static struct e1000_gc_device gc_adapters[E1000_GC_MAX_ADAPTERS];

struct e1000_gc_discovery {
    struct pci_device_info pci[E1000_GC_MAX_ADAPTERS];
    uint32_t count;
};

static uint32_t gc_reg_read(struct e1000_gc_device *device, uint32_t offset){
    return *(volatile uint32_t*)(device->registers+offset);
}

static void gc_reg_write(struct e1000_gc_device *device, uint32_t offset,
                         uint32_t value){
    *(volatile uint32_t*)(device->registers+offset)=value;
}

static void gc_split_address(struct e1000_gc_device *device, uint64_t address,
                             uint32_t low_register, uint32_t high_register){
    gc_reg_write(device,low_register,(uint32_t)address);
    gc_reg_write(device,high_register,(uint32_t)(address>>32));
}

static void gc_release_dma(struct e1000_gc_device *device){
    for(uint32_t index=0;index<E1000_GC_RING_COUNT;index++){
        if(device->rx_buffer_physical[index])
            pmm_free_page(device->rx_buffer_physical[index]);
        if(device->tx_buffer_physical[index])
            pmm_free_page(device->tx_buffer_physical[index]);
    }
    if(device->rx_ring_physical) pmm_free_page(device->rx_ring_physical);
    if(device->tx_ring_physical) pmm_free_page(device->tx_ring_physical);
    device->rx_ring_physical=0;
    device->tx_ring_physical=0;
}

static bool gc_allocate_dma(struct e1000_gc_device *device){
    device->rx_ring_physical=pmm_allocate_page();
    device->tx_ring_physical=pmm_allocate_page();
    if(!device->rx_ring_physical || !device->tx_ring_physical){
        gc_release_dma(device);
        return false;
    }
    device->rx_ring=pmm_physical_to_virtual(device->rx_ring_physical);
    device->tx_ring=pmm_physical_to_virtual(device->tx_ring_physical);
    for(uint32_t index=0;index<E1000_GC_RING_COUNT;index++){
        device->rx_buffer_physical[index]=pmm_allocate_page();
        device->tx_buffer_physical[index]=pmm_allocate_page();
        if(!device->rx_buffer_physical[index]
           || !device->tx_buffer_physical[index]){
            gc_release_dma(device);
            return false;
        }
        device->rx_buffers[index]=pmm_physical_to_virtual(
            device->rx_buffer_physical[index]);
        device->tx_buffers[index]=pmm_physical_to_virtual(
            device->tx_buffer_physical[index]);
        device->rx_ring[index].address=device->rx_buffer_physical[index];
        device->tx_ring[index].address=device->tx_buffer_physical[index];
        device->tx_ring[index].status=E1000_GC_TX_STATUS_DD;
    }
    return true;
}

static bool gc_read_mac(struct e1000_gc_device *device, uint8_t mac[6]){
    uint32_t low=gc_reg_read(device,E1000_GC_REG_RAL);
    uint32_t high=gc_reg_read(device,E1000_GC_REG_RAH);
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

static void gc_program_receive_address(struct e1000_gc_device *device,
                                       const uint8_t mac[6]){
    uint32_t low=(uint32_t)mac[0]|((uint32_t)mac[1]<<8)
        |((uint32_t)mac[2]<<16)|((uint32_t)mac[3]<<24);
    uint32_t high=(uint32_t)mac[4]|((uint32_t)mac[5]<<8)|(1U<<31);
    gc_reg_write(device,E1000_GC_REG_RAL,low);
    gc_reg_write(device,E1000_GC_REG_RAH,high);
}

static bool gc_reset_controller(struct e1000_gc_device *device){
    gc_reg_write(device,E1000_GC_REG_IMC,0xFFFFFFFFU);
    (void)gc_reg_read(device,E1000_GC_REG_ICR);
    gc_reg_write(device,E1000_GC_REG_RCTL,0);
    gc_reg_write(device,E1000_GC_REG_TCTL,E1000_GC_TCTL_PSP);
    (void)gc_reg_read(device,E1000_GC_REG_ICR);
    timer_sleep(10);
    uint32_t ctrl=gc_reg_read(device,E1000_GC_REG_CTRL);
    gc_reg_write(device,E1000_GC_REG_CTRL,ctrl|E1000_GC_CTRL_RST);
    for(uint32_t attempt=0;attempt<E1000_GC_POLL_TIMEOUT;attempt++){
        if(!(gc_reg_read(device,E1000_GC_REG_CTRL)&E1000_GC_CTRL_RST)){
            gc_reg_write(device,E1000_GC_REG_IMC,0xFFFFFFFFU);
            (void)gc_reg_read(device,E1000_GC_REG_ICR);
            return true;
        }
        __asm__ volatile("pause");
    }
    return false;
}

static bool gc_mdio_read(struct e1000_gc_device *device, uint8_t phy,
                         uint8_t reg, uint16_t *value){
    gc_reg_write(device,E1000_GC_REG_MDIC,
                 ((uint32_t)reg<<E1000_GC_MDIC_REG_SHIFT)
                 |((uint32_t)phy<<E1000_GC_MDIC_PHY_SHIFT)
                 |E1000_GC_MDIC_OP_READ);
    for(uint32_t attempt=0;attempt<E1000_GC_MDIO_TIMEOUT;attempt++){
        uint32_t mdic=gc_reg_read(device,E1000_GC_REG_MDIC);
        if(mdic&E1000_GC_MDIC_READY){
            if(mdic&E1000_GC_MDIC_ERROR) return false;
            *value=(uint16_t)(mdic&E1000_GC_MDIC_DATA_MASK);
            return true;
        }
        __asm__ volatile("pause");
    }
    return false;
}

static bool gc_mdio_write(struct e1000_gc_device *device, uint8_t phy,
                          uint8_t reg, uint16_t value){
    gc_reg_write(device,E1000_GC_REG_MDIC,
                 (uint32_t)value
                 |((uint32_t)reg<<E1000_GC_MDIC_REG_SHIFT)
                 |((uint32_t)phy<<E1000_GC_MDIC_PHY_SHIFT)
                 |E1000_GC_MDIC_OP_WRITE);
    for(uint32_t attempt=0;attempt<E1000_GC_MDIO_TIMEOUT;attempt++){
        uint32_t mdic=gc_reg_read(device,E1000_GC_REG_MDIC);
        if(mdic&E1000_GC_MDIC_READY)
            return !(mdic&E1000_GC_MDIC_ERROR);
        __asm__ volatile("pause");
    }
    return false;
}

static bool gc_phy_probe(struct e1000_gc_device *device){
    for(uint8_t pass=0;pass<2;pass++){
        for(uint8_t addr=0;addr<32;addr++){
            if(pass==0&&addr!=1) continue;
            uint16_t id1=0,id2=0;
            if(!gc_mdio_read(device,addr,E1000_GC_MII_PHYSID1,&id1))
                continue;
            if(!gc_mdio_read(device,addr,E1000_GC_MII_PHYSID2,&id2))
                continue;
            if(id1==0xFFFF||id1==0x0000) continue;
            device->phy_addr=addr;
            klogf(KLOG_OK,"e1000: 82543GC PHY id %04x:%04x"
                  " at MDIO address %u",id1,id2,addr);
            return true;
        }
    }
    return false;
}

static void gc_decode_pssr(struct e1000_gc_device *device, uint16_t pssr){
    device->full_duplex=(pssr&E1000_GC_M88_PSSR_DPLX)!=0;
    switch(pssr&E1000_GC_M88_PSSR_SPEED_MASK){
    case E1000_GC_M88_PSSR_1000MBS: device->link_speed_mbps=1000; break;
    case E1000_GC_M88_PSSR_100MBS: device->link_speed_mbps=100; break;
    default: device->link_speed_mbps=10; break;
    }
}

static void gc_program_mac(struct e1000_gc_device *device){
    uint32_t ctrl=gc_reg_read(device,E1000_GC_REG_CTRL);
    ctrl|=E1000_GC_CTRL_SLU|E1000_GC_CTRL_FRCSPD|E1000_GC_CTRL_FRCDPX;
    ctrl&=~(E1000_GC_CTRL_SPD_SEL|E1000_GC_CTRL_ILOS);
    if(device->full_duplex) ctrl|=E1000_GC_CTRL_FD;
    if(device->link_speed_mbps==1000) ctrl|=E1000_GC_CTRL_SPD_1000;
    else if(device->link_speed_mbps==100) ctrl|=E1000_GC_CTRL_SPD_100;
    gc_reg_write(device,E1000_GC_REG_CTRL,ctrl);
}

static bool gc_setup_link(struct e1000_gc_device *device){
    timer_sleep(20);
    if(gc_reg_read(device,E1000_GC_REG_STATUS)&E1000_GC_STATUS_LU){
        uint16_t pssr=0;
        uint16_t link_bits=E1000_GC_M88_PSSR_LINK|E1000_GC_M88_PSSR_RESOLVED;
        if(gc_phy_probe(device)
           && gc_mdio_read(device,device->phy_addr,E1000_GC_M88_PSSR,&pssr)
           && (pssr&link_bits)==link_bits)
            gc_decode_pssr(device,pssr);
        else{
            device->link_speed_mbps=1000;
            device->full_duplex=true;
        }
        gc_program_mac(device);
        return true;
    }
    uint32_t ext=gc_reg_read(device,E1000_GC_REG_CTRL_EXT);
    gc_reg_write(device,E1000_GC_REG_CTRL_EXT,
                 (ext|E1000_GC_CTRL_EXT_SDP4_DIR)
                 &~E1000_GC_CTRL_EXT_SDP4_DATA);
    timer_sleep(10);
    gc_reg_write(device,E1000_GC_REG_CTRL_EXT,
                 ext|E1000_GC_CTRL_EXT_SDP4_DIR|E1000_GC_CTRL_EXT_SDP4_DATA);
    timer_sleep(1);

    uint32_t ctrl=gc_reg_read(device,E1000_GC_REG_CTRL);
    ctrl|=E1000_GC_CTRL_SLU|E1000_GC_CTRL_FRCSPD|E1000_GC_CTRL_FRCDPX;
    gc_reg_write(device,E1000_GC_REG_CTRL,ctrl);

    if(!gc_phy_probe(device)){
        klog(KLOG_WARN,"e1000: 82543GC: no PHY on MDIC,"
             " continuing with 1000/full");
        device->link_speed_mbps=1000;
        device->full_duplex=true;
        gc_program_mac(device);
        return true;
    }

    gc_mdio_write(device,device->phy_addr,E1000_GC_MII_ANAR,
                  E1000_GC_ANAR_10H_10F_100H_100F|E1000_GC_ANAR_PAUSE_SYM);
    gc_mdio_write(device,device->phy_addr,E1000_GC_MII_1000T_CTRL,
                  E1000_GC_1000T_ADV_FULL);
    gc_mdio_write(device,device->phy_addr,E1000_GC_MII_BMCR,
                  E1000_GC_BMCR_ANENABLE|E1000_GC_BMCR_ANRESTART);

    uint64_t start=timer_ticks();
    bool negotiated=false;
    while(timer_ticks()-start<E1000_GC_AN_TIMEOUT_MS){
        uint16_t first=0,second=0;
        if(gc_mdio_read(device,device->phy_addr,E1000_GC_MII_BMSR,&first)
           && gc_mdio_read(device,device->phy_addr,E1000_GC_MII_BMSR,&second)
           && (second&E1000_GC_BMSR_ANEGCOMPLETE)){
            negotiated=true;
            break;
        }
        timer_sleep(1);
    }
    if(!negotiated)
        klog(KLOG_WARN,"e1000: 82543GC: auto-negotiation timed out");

    start=timer_ticks();
    uint16_t pssr=0;
    bool resolved=false;
    while(timer_ticks()-start<E1000_GC_PHY_LINK_TIMEOUT_MS){
        uint16_t link_bits=E1000_GC_M88_PSSR_LINK|E1000_GC_M88_PSSR_RESOLVED;
        if(gc_mdio_read(device,device->phy_addr,E1000_GC_M88_PSSR,&pssr)
           && (pssr&link_bits)==link_bits){
            resolved=true;
            break;
        }
        timer_sleep(1);
    }

    if(resolved){
        gc_decode_pssr(device,pssr);
    }else if(gc_reg_read(device,E1000_GC_REG_STATUS)&E1000_GC_STATUS_LU){
        device->link_speed_mbps=1000;
        device->full_duplex=true;
        klog(KLOG_WARN,"e1000: 82543GC: PHY status unreadable,"
             " assuming 1000/full");
    }else{
        device->link_speed_mbps=1000;
        device->full_duplex=true;
        klog(KLOG_WARN,"e1000: 82543GC: no link, interface stays down");
    }

    gc_program_mac(device);
    return true;
}

static void gc_initialize_receive(struct e1000_gc_device *device){
    gc_split_address(device,device->rx_ring_physical,
                     E1000_GC_REG_RDBAL,E1000_GC_REG_RDBAH);
    gc_reg_write(device,E1000_GC_REG_RDLEN,
                 E1000_GC_RING_COUNT*sizeof(struct e1000_gc_rx_descriptor));
    gc_reg_write(device,E1000_GC_REG_RDH,0);
    gc_reg_write(device,E1000_GC_REG_RDT,E1000_GC_RING_COUNT-1);
    device->rx_next=0;
    gc_reg_write(device,E1000_GC_REG_RCTL,
                 E1000_GC_RCTL_EN|E1000_GC_RCTL_BAM|E1000_GC_RCTL_SECRC);
}

static void gc_initialize_transmit(struct e1000_gc_device *device){
    gc_split_address(device,device->tx_ring_physical,
                     E1000_GC_REG_TDBAL,E1000_GC_REG_TDBAH);
    gc_reg_write(device,E1000_GC_REG_TDLEN,
                 E1000_GC_RING_COUNT*sizeof(struct e1000_gc_tx_descriptor));
    gc_reg_write(device,E1000_GC_REG_TDH,0);
    gc_reg_write(device,E1000_GC_REG_TDT,0);
    device->tx_next=0;
    uint32_t ipgt=10,ipgr1=10,ipgr2=10;
    if(device->link_speed_mbps==1000){ ipgr1=8; ipgr2=6; }
    gc_reg_write(device,E1000_GC_REG_TIPG,ipgt|(ipgr1<<10)|(ipgr2<<20));
    gc_reg_write(device,E1000_GC_REG_TCTL,E1000_GC_TCTL_EN|E1000_GC_TCTL_PSP
                 |(15U<<E1000_GC_TCTL_CT_SHIFT)
                 |(E1000_GC_TCTL_COLD<<E1000_GC_TCTL_COLD_SHIFT));
}

static bool gc_transmit(void *context, const uint8_t *frame,
                        uint16_t length){
    struct e1000_gc_device *device=context;
    if(!device || !device->initialized || length>E1000_GC_DMA_BUFFER_SIZE)
        return false;
    if(__atomic_test_and_set(&device->tx_locked,__ATOMIC_ACQUIRE)) return false;
    uint16_t index=device->tx_next;
    volatile struct e1000_gc_tx_descriptor *descriptor=&device->tx_ring[index];
    if(!(descriptor->status&E1000_GC_TX_STATUS_DD)){
        __atomic_clear(&device->tx_locked,__ATOMIC_RELEASE);
        return false;
    }
    memcpy(device->tx_buffers[index],frame,length);
    descriptor->length=length;
    descriptor->checksum_offset=0;
    descriptor->checksum_start=0;
    descriptor->special=0;
    descriptor->status=0;
    descriptor->command=E1000_GC_TX_CMD_EOP|E1000_GC_TX_CMD_IFCS
        |E1000_GC_TX_CMD_RS;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    device->tx_next=(uint16_t)((index+1)%E1000_GC_RING_COUNT);
    gc_reg_write(device,E1000_GC_REG_TDT,device->tx_next);
    __atomic_clear(&device->tx_locked,__ATOMIC_RELEASE);
    return true;
}

static void gc_poll(void *context, uint32_t budget){
    struct e1000_gc_device *device=context;
    if(!device || !device->initialized) return;
    for(uint32_t completed=0;completed<budget;completed++){
        uint16_t index=device->rx_next;
        volatile struct e1000_gc_rx_descriptor *descriptor=
            &device->rx_ring[index];
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if(!(descriptor->status&E1000_GC_RX_STATUS_DD)) break;
        if(!descriptor->errors && (descriptor->status&E1000_GC_RX_STATUS_EOP)
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
        gc_reg_write(device,E1000_GC_REG_RDT,index);
        device->rx_next=(uint16_t)((index+1)%E1000_GC_RING_COUNT);
    }
}

static bool gc_link_up(void *context){
    struct e1000_gc_device *device=context;
    return device && device->initialized
        && (gc_reg_read(device,E1000_GC_REG_STATUS)&E1000_GC_STATUS_LU)!=0;
}

static const struct net_device_ops gc_net_ops={
    .transmit=gc_transmit,
    .poll=gc_poll,
    .link_up=gc_link_up
};

static void gc_find_adapters(const struct pci_device_info *device,
                             void *context){
    struct e1000_gc_discovery *found=context;
    if(device->vendor_id!=E1000_GC_VENDOR_INTEL) return;
    if(found->count>=E1000_GC_MAX_ADAPTERS) return;
    if(device->device_id==E1000_GC_DEVICE_82543GC_FIBER){
        klogf(KLOG_WARN,"e1000: 82543GC-Fiber at %02x:%02x.%u"
              " needs fiber TBI, skipped",
              device->bus,device->slot,device->function);
        return;
    }
    if(device->device_id!=E1000_GC_DEVICE_82543GC_COPPER
       && device->device_id!=E1000_GC_DEVICE_82544GC_COPPER) return;
    found->pci[found->count]=*device;
    found->count++;
}

static bool gc_init_one(struct e1000_gc_device *device,
                        const struct pci_device_info *pci){
    memset(device,0,sizeof(*device));
    device->pci=*pci;
    device->link_speed_mbps=1000;
    device->full_duplex=true;

    uint32_t bar0=pci_read_config32(pci->bus,pci->slot,
                                    pci->function,0x10);
    if(!bar0 || bar0==0xFFFFFFFFU || (bar0&1U)){
        klog(KLOG_ERROR,"e1000: 82543GC: BAR0 is not a memory BAR");
        return false;
    }
    if(!pci_update_command(pci,
                           PCI_COMMAND_MEMORY|PCI_COMMAND_BUS_MASTER,0)){
        klog(KLOG_ERROR,"e1000: 82543GC: failed to enable PCI memory"
             " and bus mastering");
        return false;
    }
    uint64_t bar_address=pci_read_bar(pci->bus,pci->slot,
                                      pci->function,0);
    device->registers=mmio_map(bar_address,E1000_GC_MMIO_SIZE);
    if(!device->registers){
        klog(KLOG_ERROR,"e1000: 82543GC: cannot map 128 KiB MMIO BAR");
        return false;
    }
    if(!gc_reset_controller(device)){
        klog(KLOG_ERROR,"e1000: 82543GC: controller reset timed out");
        return false;
    }
    if(!gc_read_mac(device,device->net.mac)){
        klog(KLOG_ERROR,"e1000: 82543GC: invalid MAC address"
             " after EEPROM reload");
        return false;
    }
    gc_program_receive_address(device,device->net.mac);
    gc_setup_link(device);
    if(!gc_allocate_dma(device)){
        klog(KLOG_ERROR,"e1000: 82543GC: cannot allocate bounded DMA rings");
        return false;
    }

    for(uint32_t mta=0;mta<128;mta++)
        gc_reg_write(device,E1000_GC_REG_MTA+mta*4,0);
    gc_initialize_receive(device);
    gc_initialize_transmit(device);
    gc_reg_write(device,E1000_GC_REG_CTRL,
                 gc_reg_read(device,E1000_GC_REG_CTRL)|E1000_GC_CTRL_SLU);

    uint32_t if_index=net_device_count();
    if(if_index>9) if_index=9;
    device->net.name[0]='e';
    device->net.name[1]='t';
    device->net.name[2]='h';
    device->net.name[3]=(char)('0'+if_index);
    device->net.name[4]='\0';
    device->net.mtu=NET_ETHERNET_MTU;
    device->net.ops=&gc_net_ops;
    device->net.driver_context=device;
    device->initialized=true;
    device->net.cached_link_up=gc_link_up(device);
    if(!net_device_register(&device->net)){
        device->initialized=false;
        gc_reg_write(device,E1000_GC_REG_RCTL,0);
        gc_reg_write(device,E1000_GC_REG_TCTL,0);
        gc_release_dma(device);
        return false;
    }
    klogf(KLOG_OK,
          "e1000: %s 8254xGC %02x:%02x:%02x:%02x:%02x:%02x %u/%s link=%s polling",
          device->net.name,
          device->net.mac[0],device->net.mac[1],device->net.mac[2],
          device->net.mac[3],device->net.mac[4],device->net.mac[5],
          device->link_speed_mbps,
          device->full_duplex ? "full" : "half",
          device->net.cached_link_up ? "up" : "down");
    return true;
}

bool e1000_82543gc_init(void){
    struct e1000_gc_discovery found;
    memset(&found,0,sizeof(found));
    pci_enumerate(gc_find_adapters,&found);
    if(!found.count) return false;

    bool ready=false;
    uint32_t index=0;
    for(uint32_t candidate=0;candidate<found.count;candidate++){
        if(gc_init_one(&gc_adapters[index],&found.pci[candidate])){
            ready=true;
            index++;
        }
    }
    return ready;
}