#include "block_device.h"
#include "ahci.h"
#include "ata_pio.h"
#include "../usb/xhci.h"
#include "../usb/ehci.h"
#include "../../lib/string.h"
#include "../../kernel/diagnostics/klog.h"
#include "../../kernel/process/scheduler.h"
#include "../interrupts/timer.h"

#define BLOCK_TRANSPORT_NONE 0

static uint8_t active_transport;
static uint32_t active_index;
static bool initialization_complete;
static bool initialization_result;
static volatile bool usb_rescan_busy;
static volatile uint32_t exclusive_io_count;
static uint64_t next_hotplug_scan;
static bool preferred_usb_valid;
static char preferred_usb_serial[STORAGE_SERIAL_CAPACITY];

static void usb_rescan_lock(void){
    while(__atomic_test_and_set(&usb_rescan_busy,__ATOMIC_ACQUIRE))
        scheduler_sleep(1);
}

static void usb_rescan_unlock(void){
    __atomic_clear(&usb_rescan_busy,__ATOMIC_RELEASE);
}

void block_device_begin_exclusive_io(void){
    usb_rescan_lock();
    (void)__atomic_add_fetch(&exclusive_io_count,1,__ATOMIC_ACQ_REL);
    usb_rescan_unlock();
}

void block_device_end_exclusive_io(void){
    uint32_t count=__atomic_load_n(&exclusive_io_count,__ATOMIC_ACQUIRE);
    if(count) (void)__atomic_sub_fetch(&exclusive_io_count,1,
                                      __ATOMIC_ACQ_REL);
}

static bool block_device_exclusive_io_active(void){
    return __atomic_load_n(&exclusive_io_count,__ATOMIC_ACQUIRE)!=0;
}

bool block_device_init(void){
    if(initialization_complete) return initialization_result;
    klog(KLOG_INFO,"storage init: probing legacy ATA PIO");
    bool ata_available=ata_pio_init();
    klogf(KLOG_INFO,"storage init: ATA PIO complete, disks=%u",ata_pio_device_count());
    klog(KLOG_INFO,"storage init: probing AHCI SATA");
    bool ahci_available=ahci_init(ata_pio_device_count());
    klogf(KLOG_INFO,"storage init: AHCI complete, disks=%u",ahci_device_count());
    klog(KLOG_INFO,"storage init: probing xHCI USB");
    bool usb_available=xhci_init(ata_pio_device_count()+ahci_device_count());
    klogf(KLOG_INFO,"storage init: xHCI complete, disks=%u",xhci_device_count());
    klog(KLOG_INFO,"storage init: probing EHCI USB 2.0");
    bool ehci_available=ehci_init(ata_pio_device_count()+ahci_device_count()
                                  +xhci_device_count());
    klogf(KLOG_INFO,"storage init: EHCI complete, disks=%u",ehci_device_count());
    if(active_transport==BLOCK_TRANSPORT_NONE){
        if(ata_available){
            active_transport=STORAGE_TRANSPORT_ATA_PIO;
            active_index=0;
            (void)ata_pio_select_device(0);
        } else if(ahci_available){
            active_transport=STORAGE_TRANSPORT_AHCI;
            active_index=0;
            (void)ahci_select_device(0);
        } else if(usb_available){
            active_transport=STORAGE_TRANSPORT_USB_MSC;
            active_index=0;
            (void)xhci_select_device(0);
        } else if(ehci_available){
            active_transport=STORAGE_TRANSPORT_USB_EHCI;
            active_index=0;
            (void)ehci_select_device(0);
        }
    }
    initialization_result=ata_available || ahci_available || usb_available || ehci_available;
    initialization_complete=true;
    return initialization_result;
}

uint32_t block_device_rescan_usb(void){
    if(block_device_exclusive_io_active()){
        klog(KLOG_INFO,"usb: rescan deferred during exclusive disk I/O");
        return xhci_device_count()+ehci_device_count();
    }
    bool restore_usb=active_transport==STORAGE_TRANSPORT_USB_MSC
        || active_transport==STORAGE_TRANSPORT_USB_EHCI
        || preferred_usb_valid;
    if(active_transport==STORAGE_TRANSPORT_USB_MSC
       || active_transport==STORAGE_TRANSPORT_USB_EHCI){
        uint32_t count=block_device_count();
        for(uint32_t index=0;index<count;index++){
            struct storage_device_info info;
            if(block_device_get_info(index,&info) && info.selected){
                memset(preferred_usb_serial,0,sizeof(preferred_usb_serial));
                strncpy(preferred_usb_serial,info.serial,
                        sizeof(preferred_usb_serial)-1);
                preferred_usb_valid=true;
                break;
            }
        }
    }
    usb_rescan_lock();
    if(block_device_exclusive_io_active()){
        uint32_t count=xhci_device_count()+ehci_device_count();
        usb_rescan_unlock();
        return count;
    }
    uint32_t fixed_count=ata_pio_device_count()+ahci_device_count();
    (void)xhci_rescan(fixed_count);
    (void)ehci_rescan(fixed_count+xhci_device_count());
    bool restored_usb=false;
    if(restore_usb){
        active_transport=BLOCK_TRANSPORT_NONE;
        uint32_t count=block_device_count();
        for(uint32_t index=0;index<count;index++){
            struct storage_device_info info;
            if(block_device_get_info(index,&info)
               && (info.transport==STORAGE_TRANSPORT_USB_MSC
                   || info.transport==STORAGE_TRANSPORT_USB_EHCI)
               && strcmp(info.serial,preferred_usb_serial)==0){
                (void)block_device_select(index);
                restored_usb=true;
                break;
            }
        }
    }
    if(active_transport==BLOCK_TRANSPORT_NONE && (!restore_usb || restored_usb)){
        if(xhci_device_count()){
            active_transport=STORAGE_TRANSPORT_USB_MSC;
            active_index=0;
            (void)xhci_select_device(0);
        } else if(ehci_device_count()){
            active_transport=STORAGE_TRANSPORT_USB_EHCI;
            active_index=0;
            (void)ehci_select_device(0);
        }
    }
    uint32_t result=xhci_device_count()+ehci_device_count();
    usb_rescan_unlock();
    return result;
}

void block_device_poll_usb_hotplug(void){
    if(block_device_exclusive_io_active()) return;
    uint64_t now=timer_ticks();
    if(now<next_hotplug_scan) return;
    next_hotplug_scan=now+1000;
    if(!xhci_topology_changed() && !ehci_topology_changed()) return;
    klog(KLOG_INFO,"usb: port topology changed, rescanning controllers");
    uint32_t count=block_device_rescan_usb();
    klogf(KLOG_INFO,"usb: hotplug rescan complete, storage devices=%u",count);
}

uint32_t block_device_count(void){
    (void)block_device_init();
    return ata_pio_device_count()+ahci_device_count()+xhci_device_count()
        +ehci_device_count();
}

bool block_device_get_info(uint32_t index, struct storage_device_info *info){
    if(!info) return false;
    (void)block_device_init();
    uint32_t ata_count=ata_pio_device_count();
    uint32_t ahci_count=ahci_device_count();
    uint32_t xhci_count=xhci_device_count();
    bool status;
    uint8_t transport;
    uint32_t transport_index;
    if(index<ata_count){
        status=ata_pio_get_device_info(index,info);
        transport=STORAGE_TRANSPORT_ATA_PIO;
        transport_index=index;
    } else if(index-ata_count<ahci_count){
        transport_index=index-ata_count;
        status=ahci_get_device_info(transport_index,info);
        transport=STORAGE_TRANSPORT_AHCI;
    } else if(index-ata_count-ahci_count<xhci_count){
        transport_index=index-ata_count-ahci_count;
        status=xhci_get_device_info(transport_index,info);
        transport=STORAGE_TRANSPORT_USB_MSC;
    } else {
        transport_index=index-ata_count-ahci_count-xhci_count;
        status=ehci_get_device_info(transport_index,info);
        transport=STORAGE_TRANSPORT_USB_EHCI;
    }
    if(status){
        if(info->sector_size==0){
            klogf(KLOG_WARN,"storage: device %s reported zero sector size, using %u",
                  info->name,BLOCK_SECTOR_SIZE);
            info->sector_size=BLOCK_SECTOR_SIZE;
        }
        info->selected=active_transport==transport && active_index==transport_index;
    }
    return status;
}

int32_t block_device_find(const char *name){
    if(!name) return -1;
    uint32_t count=block_device_count();
    for(uint32_t index=0;index<count;index++){
        struct storage_device_info info;
        if(block_device_get_info(index,&info) && strcmp(info.name,name)==0){
            return (int32_t)index;
        }
    }
    return -1;
}

int32_t block_device_list(struct storage_device_info *devices, uint32_t capacity){
    if(!devices || capacity==0 || capacity>0x7FFFFFFF) return -1;
    (void)block_device_init();
    uint32_t available=block_device_count();
    uint32_t count=0;
    while(count<available && count<capacity){
        if(!block_device_get_info(count,&devices[count])) return -1;
        count++;
    }
    return (int32_t)count;
}

bool block_device_select(uint32_t index){
    (void)block_device_init();
    uint32_t ata_count=ata_pio_device_count();
    if(index<ata_count){
        if(!ata_pio_select_device(index)) return false;
        active_transport=STORAGE_TRANSPORT_ATA_PIO;
        active_index=index;
        preferred_usb_valid=false;
        return true;
    }
    uint32_t ahci_count=ahci_device_count();
    uint32_t xhci_count=xhci_device_count();
    uint32_t relative=index-ata_count;
    if(relative<ahci_count){
        if(!ahci_select_device(relative)) return false;
        active_transport=STORAGE_TRANSPORT_AHCI;
        active_index=relative;
        preferred_usb_valid=false;
        return true;
    }
    uint32_t usb_index=relative-ahci_count;
    if(usb_index<xhci_count){
        if(!xhci_select_device(usb_index)) return false;
        active_transport=STORAGE_TRANSPORT_USB_MSC;
        active_index=usb_index;
        struct storage_device_info info;
        if(xhci_get_device_info(usb_index,&info)){
            memset(preferred_usb_serial,0,sizeof(preferred_usb_serial));
            strncpy(preferred_usb_serial,info.serial,
                    sizeof(preferred_usb_serial)-1);
            preferred_usb_valid=true;
        }
        return true;
    }
    uint32_t ehci_index=usb_index-xhci_count;
    if(!ehci_select_device(ehci_index)) return false;
    active_transport=STORAGE_TRANSPORT_USB_EHCI;
    active_index=ehci_index;
    struct storage_device_info info;
    if(ehci_get_device_info(ehci_index,&info)){
        memset(preferred_usb_serial,0,sizeof(preferred_usb_serial));
        strncpy(preferred_usb_serial,info.serial,
                sizeof(preferred_usb_serial)-1);
        preferred_usb_valid=true;
    }
    return true;
}

bool block_device_read(uint32_t lba, void *buffer){
    if(active_transport==STORAGE_TRANSPORT_USB_EHCI){
        usb_rescan_lock();
        bool result=ehci_read_sector(lba,buffer);
        usb_rescan_unlock();
        return result;
    }
    if(active_transport==STORAGE_TRANSPORT_USB_MSC){
        usb_rescan_lock();
        bool result=xhci_read_sector(lba,buffer);
        usb_rescan_unlock();
        return result;
    }
    if(active_transport==STORAGE_TRANSPORT_AHCI) return ahci_read_sector(lba,buffer);
    if(active_transport==STORAGE_TRANSPORT_ATA_PIO) return ata_pio_read_sector(lba,buffer);
    return false;
}

bool block_device_write(uint32_t lba, const void *buffer){
    if(active_transport==STORAGE_TRANSPORT_USB_EHCI){
        usb_rescan_lock();
        bool result=ehci_write_sector(lba,buffer);
        usb_rescan_unlock();
        return result;
    }
    if(active_transport==STORAGE_TRANSPORT_USB_MSC){
        usb_rescan_lock();
        bool result=xhci_write_sector(lba,buffer);
        usb_rescan_unlock();
        return result;
    }
    if(active_transport==STORAGE_TRANSPORT_AHCI) return ahci_write_sector(lba,buffer);
    if(active_transport==STORAGE_TRANSPORT_ATA_PIO) return ata_pio_write_sector(lba,buffer);
    return false;
}

bool block_device_flush(void){
    if(active_transport==STORAGE_TRANSPORT_AHCI) return ahci_flush();
    if(active_transport==STORAGE_TRANSPORT_ATA_PIO) return ata_pio_flush();
    return true;
}

const char *block_device_name(void){
    if(active_transport==STORAGE_TRANSPORT_USB_EHCI) return ehci_device_name();
    if(active_transport==STORAGE_TRANSPORT_USB_MSC) return xhci_device_name();
    if(active_transport==STORAGE_TRANSPORT_AHCI) return ahci_device_name();
    if(active_transport==STORAGE_TRANSPORT_ATA_PIO) return ata_pio_device_name();
    return "none";
}
