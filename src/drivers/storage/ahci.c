#include "ahci.h"

#include "storage_probe.h"
#include "../pci/pci.h"
#include "../../arch/x86_64/mmio.h"
#include "../../kernel/diagnostics/klog.h"
#include "../../lib/string.h"

#define AHCI_DEVICE_LIMIT       8
#define AHCI_PORT_LIMIT        32
#define AHCI_COMMAND_SLOT_LIMIT 32
#define AHCI_TIMEOUT            10000000U
#define AHCI_MMIO_MAP_SIZE      0x2000U

#define AHCI_GHC_AE             (1U<<31)
#define AHCI_CAP_64_BIT         (1U<<31)
#define AHCI_PORT_CMD_ST        (1U<<0)
#define AHCI_PORT_CMD_FRE       (1U<<4)
#define AHCI_PORT_CMD_FR        (1U<<14)
#define AHCI_PORT_CMD_CR        (1U<<15)
#define AHCI_PORT_IS_TFES       (1U<<30)
#define AHCI_TFD_BUSY           0x80
#define AHCI_TFD_DRQ            0x08
#define AHCI_SSTS_DET_PRESENT   0x03
#define AHCI_SSTS_IPM_ACTIVE    0x01
#define AHCI_SIGNATURE_ATA      0x00000101
#define AHCI_FIS_HOST_TO_DEVICE 0x27
#define ATA_COMMAND_IDENTIFY    0xEC
#define ATA_COMMAND_READ_DMA    0xC8
#define ATA_COMMAND_WRITE_DMA   0xCA
#define ATA_COMMAND_READ_DMA_EXT  0x25
#define ATA_COMMAND_WRITE_DMA_EXT 0x35
#define ATA_COMMAND_FLUSH       0xE7
#define ATA_COMMAND_FLUSH_EXT   0xEA
#define AHCI_NO_DEVICE          0xFF

#define HBA_CAP 0
#define HBA_GHC 1
#define HBA_PI  3
#define PORT_CLB   0
#define PORT_CLBU  1
#define PORT_FB    2
#define PORT_FBU   3
#define PORT_IS    4
#define PORT_IE    5
#define PORT_CMD   6
#define PORT_TFD   8
#define PORT_SIG   9
#define PORT_SSTS 10
#define PORT_SERR 12
#define PORT_CI   14

struct ahci_command_header {
    uint16_t flags;
    uint16_t prdt_length;
    uint32_t transferred_bytes;
    uint32_t command_table_base;
    uint32_t command_table_base_upper;
    uint32_t reserved[4];
};

struct ahci_prdt_entry {
    uint32_t data_base;
    uint32_t data_base_upper;
    uint32_t reserved;
    uint32_t byte_count;
};

struct ahci_command_table {
    uint8_t command_fis[64];
    uint8_t atapi_command[16];
    uint8_t reserved[48];
    struct ahci_prdt_entry prdt[1];
};

struct ahci_device {
    struct storage_device_info info;
    volatile uint32_t *port;
    bool lba48;
};

_Static_assert(sizeof(struct ahci_command_header)==32,"AHCI command header size");
_Static_assert(sizeof(struct ahci_prdt_entry)==16,"AHCI PRDT entry size");
_Static_assert(sizeof(struct ahci_command_table)==144,"AHCI command table size");

static struct ahci_device devices[AHCI_DEVICE_LIMIT];
static struct ahci_command_header command_list[AHCI_COMMAND_SLOT_LIMIT]
    __attribute__((aligned(1024)));
static uint8_t received_fis[256] __attribute__((aligned(256)));
static struct ahci_command_table command_table __attribute__((aligned(128)));
static uint16_t identify_words[256] __attribute__((aligned(512)));
static uint8_t dma_buffer[512] __attribute__((aligned(512)));
static uint64_t kernel_physical_base;
static uint64_t kernel_virtual_base;
static struct ahci_probe_stats probe_stats;
static uint8_t device_count;
static uint8_t selected_device=AHCI_NO_DEVICE;
static bool mapping_ready;
static bool probe_complete;

void ahci_set_address_mapping(uint64_t direct_map_offset, uint64_t physical_base,
                              uint64_t virtual_base){
    (void)direct_map_offset;
    kernel_physical_base=physical_base;
    kernel_virtual_base=virtual_base;
    mapping_ready=true;
}

static uint64_t kernel_pointer_physical(const void *pointer){
    return (uint64_t)(uintptr_t)pointer-kernel_virtual_base+kernel_physical_base;
}

static volatile uint32_t *port_registers(volatile uint32_t *hba, uint8_t port){
    return hba+(0x100+port*0x80)/sizeof(uint32_t);
}

static bool stop_port(volatile uint32_t *port){
    port[PORT_CMD]&=~AHCI_PORT_CMD_ST;
    for(uint32_t wait=0;wait<AHCI_TIMEOUT;wait++){
        if(!(port[PORT_CMD]&AHCI_PORT_CMD_CR)) break;
        if(wait+1==AHCI_TIMEOUT) return false;
    }
    port[PORT_CMD]&=~AHCI_PORT_CMD_FRE;
    for(uint32_t wait=0;wait<AHCI_TIMEOUT;wait++){
        if(!(port[PORT_CMD]&AHCI_PORT_CMD_FR)) return true;
    }
    return false;
}

static bool start_port(volatile uint32_t *port){
    for(uint32_t wait=0;wait<AHCI_TIMEOUT;wait++){
        if(!(port[PORT_CMD]&AHCI_PORT_CMD_CR)){
            port[PORT_CMD]|=AHCI_PORT_CMD_FRE;
            port[PORT_CMD]|=AHCI_PORT_CMD_ST;
            return true;
        }
    }
    return false;
}

static bool port_has_sata_disk(volatile uint32_t *port){
    uint32_t status=port[PORT_SSTS];
    uint8_t detection=(uint8_t)(status&0x0F);
    uint8_t power=(uint8_t)((status>>8)&0x0F);
    return detection==AHCI_SSTS_DET_PRESENT && power==AHCI_SSTS_IPM_ACTIVE
        && port[PORT_SIG]==AHCI_SIGNATURE_ATA;
}

static bool is_addressed_command(uint8_t command){
    return command==ATA_COMMAND_READ_DMA || command==ATA_COMMAND_WRITE_DMA
        || command==ATA_COMMAND_READ_DMA_EXT || command==ATA_COMMAND_WRITE_DMA_EXT;
}

static bool issue_command(volatile uint32_t *port, uint8_t command, uint32_t lba,
                          void *data, bool write, bool lba48){
    if(!stop_port(port)) return false;

    memset(command_list,0,sizeof(command_list));
    memset(received_fis,0,sizeof(received_fis));
    memset(&command_table,0,sizeof(command_table));

    uint64_t command_list_physical=kernel_pointer_physical(command_list);
    uint64_t received_fis_physical=kernel_pointer_physical(received_fis);
    uint64_t command_table_physical=kernel_pointer_physical(&command_table);
    uint64_t data_physical=data ? kernel_pointer_physical(data) : 0;

    port[PORT_CLB]=(uint32_t)command_list_physical;
    port[PORT_CLBU]=(uint32_t)(command_list_physical>>32);
    port[PORT_FB]=(uint32_t)received_fis_physical;
    port[PORT_FBU]=(uint32_t)(received_fis_physical>>32);
    port[PORT_IE]=0;
    port[PORT_IS]=0xFFFFFFFF;
    port[PORT_SERR]=0xFFFFFFFF;

    command_list[0].flags=(uint16_t)(5|(write ? 1<<6 : 0));
    command_list[0].prdt_length=data ? 1 : 0;
    command_list[0].command_table_base=(uint32_t)command_table_physical;
    command_list[0].command_table_base_upper=(uint32_t)(command_table_physical>>32);
    command_table.command_fis[0]=AHCI_FIS_HOST_TO_DEVICE;
    command_table.command_fis[1]=0x80;
    command_table.command_fis[2]=command;
    if(is_addressed_command(command)){
        command_table.command_fis[4]=(uint8_t)lba;
        command_table.command_fis[5]=(uint8_t)(lba>>8);
        command_table.command_fis[6]=(uint8_t)(lba>>16);
        command_table.command_fis[7]=0x40;
        if(lba48){
            command_table.command_fis[8]=(uint8_t)(lba>>24);
        } else {
            command_table.command_fis[7]|=(uint8_t)((lba>>24)&0x0F);
        }
        command_table.command_fis[12]=1;
    }
    if(data){
        command_table.prdt[0].data_base=(uint32_t)data_physical;
        command_table.prdt[0].data_base_upper=(uint32_t)(data_physical>>32);
        command_table.prdt[0].byte_count=(1U<<31)|511;
    }

    __sync_synchronize();
    if(!start_port(port)) return false;
    for(uint32_t wait=0;wait<AHCI_TIMEOUT;wait++){
        if(!(port[PORT_TFD]&(AHCI_TFD_BUSY|AHCI_TFD_DRQ))) break;
        if(wait+1==AHCI_TIMEOUT){ (void)stop_port(port); return false; }
    }

    port[PORT_CI]=1;
    for(uint32_t wait=0;wait<AHCI_TIMEOUT;wait++){
        if(port[PORT_IS]&AHCI_PORT_IS_TFES){ (void)stop_port(port); return false; }
        if(!(port[PORT_CI]&1)){
            __sync_synchronize();
            (void)stop_port(port);
            return true;
        }
    }
    (void)stop_port(port);
    return false;
}

static bool issue_identify(volatile uint32_t *port){
    memset(identify_words,0,sizeof(identify_words));
    return issue_command(port,ATA_COMMAND_IDENTIFY,0,identify_words,false,false);
}

static void copy_identify_text(char *output, uint32_t capacity,
                               uint16_t first_word, uint16_t word_count){
    uint32_t length=(uint32_t)word_count*2;
    if(length>=capacity) length=capacity-1;
    for(uint32_t index=0;index<length;index++){
        uint16_t word=identify_words[first_word+index/2];
        output[index]=(char)(index&1 ? word&0xFF : word>>8);
    }
    while(length>0 && output[length-1]==' ') length--;
    output[length]='\0';
}

static void fill_device_info(struct ahci_device *device, uint8_t name_index,
                             uint8_t controller, uint8_t port_index,
                             volatile uint32_t *port){
    struct storage_device_info *info=&device->info;
    memset(info,0,sizeof(*info));
    info->name[0]='/';
    info->name[1]='d';
    info->name[2]='e';
    info->name[3]='v';
    info->name[4]='/';
    info->name[5]='s';
    info->name[6]='d';
    info->name[7]=(char)('a'+name_index);
    info->sector_count=(uint32_t)identify_words[60]
        |((uint32_t)identify_words[61]<<16);
    if(identify_words[83]&(1<<10)){
        uint64_t lba48=(uint64_t)identify_words[100]
            |((uint64_t)identify_words[101]<<16)
            |((uint64_t)identify_words[102]<<32)
            |((uint64_t)identify_words[103]<<48);
        if(lba48) info->sector_count=lba48;
    }
    info->sector_size=512;
    info->transport=STORAGE_TRANSPORT_AHCI;
    info->controller=controller;
    info->port=port_index;
    info->writable=1;
    info->operational=1;
    device->port=port;
    device->lba48=(identify_words[83]&(1<<10))!=0;
    copy_identify_text(info->serial,STORAGE_SERIAL_CAPACITY,10,10);
    copy_identify_text(info->model,STORAGE_MODEL_CAPACITY,27,20);
    if(!info->serial[0]){
        const char *hex="0123456789ABCDEF";
        memcpy(info->serial,"AHCI-",5);
        for(uint8_t i=0;i<4;i++) info->serial[5+i]=hex[(controller>> (12 - i*4)) &0xF];
        info->serial[9]='-';
        for(uint8_t i=0;i<4;i++) info->serial[10+i]=hex[(port_index>> (12 - i*4)) &0xF];
        info->serial[14]='-';
        info->serial[15]='P';
        info->serial[16]=hex[(port_index>>4)&0xF];
        info->serial[17]=hex[port_index&0xF];
        info->serial[18]=0;
    }
    if(!info->model[0]) memcpy(info->model,"AHCI Disk",10);
}

bool ahci_init(uint32_t linux_name_base){
    if(probe_complete) return device_count>0;
    probe_complete=true;
    if(!mapping_ready) return false;

    struct storage_controller_info controllers[8];
    int32_t count=storage_controller_list(controllers,8);
    if(count<0) return false;
    uint8_t ahci_index=0;
    for(int32_t index=0;index<count && device_count<AHCI_DEVICE_LIMIT;index++){
        if(controllers[index].type!=STORAGE_CONTROLLER_AHCI) continue;
        probe_stats.controllers++;
        uint64_t abar=controllers[index].register_base;
        if(abar==0){ ahci_index++; continue; }

        uint32_t pci_command=pci_read_config32(controllers[index].bus,
                                               controllers[index].slot,
                                               controllers[index].function,0x04);
        pci_write_config32(controllers[index].bus,controllers[index].slot,
                           controllers[index].function,0x04,pci_command|0x06);
        volatile uint32_t *hba=(volatile uint32_t*)mmio_map(abar,AHCI_MMIO_MAP_SIZE);
        if(!hba){
            klogf(KLOG_ERROR,"ahci%u: cannot map BAR 0x%llx",ahci_index,abar);
            probe_stats.identify_failures++;
            ahci_index++;
            continue;
        }
        klogf(KLOG_INFO,"ahci%u: BAR phys=0x%llx mapped=%p",ahci_index,abar,
              (void*)hba);
        uint64_t dma_addresses=kernel_pointer_physical(command_list)
            |kernel_pointer_physical(received_fis)
            |kernel_pointer_physical(&command_table)
            |kernel_pointer_physical(identify_words)
            |kernel_pointer_physical(dma_buffer);
        if((dma_addresses>>32)!=0 && !(hba[HBA_CAP]&AHCI_CAP_64_BIT)){
            ahci_index++;
            continue;
        }
        hba[HBA_GHC]|=AHCI_GHC_AE;
        uint32_t implemented_ports=hba[HBA_PI];
        for(uint8_t port_index=0;port_index<AHCI_PORT_LIMIT
            && device_count<AHCI_DEVICE_LIMIT;port_index++){
            if(!(implemented_ports&(1U<<port_index))) continue;
            probe_stats.implemented_ports++;
            volatile uint32_t *port=port_registers(hba,port_index);
            if(!port_has_sata_disk(port)) continue;
            probe_stats.sata_ports++;
            if(!issue_identify(port)){
                probe_stats.identify_failures++;
                continue;
            }
            fill_device_info(&devices[device_count],
                             (uint8_t)(linux_name_base+device_count),
                             ahci_index,port_index,port);
            if(devices[device_count].info.sector_count) device_count++;
        }
        ahci_index++;
    }
    return device_count>0;
}

uint32_t ahci_device_count(void){ return device_count; }

bool ahci_get_device_info(uint32_t index, struct storage_device_info *info){
    if(!info || index>=device_count) return false;
    *info=devices[index].info;
    return true;
}

bool ahci_select_device(uint32_t index){
    if(index>=device_count) return false;
    selected_device=(uint8_t)index;
    return true;
}

bool ahci_read_sector(uint32_t lba, void *buffer){
    if(!buffer || selected_device>=device_count) return false;
    struct ahci_device *device=&devices[selected_device];
    if(lba>=device->info.sector_count) return false;
    if(!device->lba48 && lba>0x0FFFFFFF) return false;
    uint8_t command=device->lba48 ? ATA_COMMAND_READ_DMA_EXT : ATA_COMMAND_READ_DMA;
    memset(dma_buffer,0,sizeof(dma_buffer));
    if(!issue_command(device->port,command,lba,dma_buffer,false,device->lba48)){
        return false;
    }
    memcpy(buffer,dma_buffer,sizeof(dma_buffer));
    return true;
}

bool ahci_write_sector(uint32_t lba, const void *buffer){
    if(!buffer || selected_device>=device_count) return false;
    struct ahci_device *device=&devices[selected_device];
    if(lba>=device->info.sector_count) return false;
    if(!device->lba48 && lba>0x0FFFFFFF) return false;
    memcpy(dma_buffer,buffer,sizeof(dma_buffer));
    uint8_t command=device->lba48 ? ATA_COMMAND_WRITE_DMA_EXT : ATA_COMMAND_WRITE_DMA;
    if(!issue_command(device->port,command,lba,dma_buffer,true,device->lba48)){
        return false;
    }
    return true;
}

bool ahci_flush(void){
    if(selected_device>=device_count) return false;
    struct ahci_device *device=&devices[selected_device];
    uint8_t command=device->lba48 ? ATA_COMMAND_FLUSH_EXT : ATA_COMMAND_FLUSH;
    return issue_command(device->port,command,0,0,false,device->lba48);
}

const char *ahci_device_name(void){
    return selected_device<device_count ? devices[selected_device].info.name : "none";
}

void ahci_get_probe_stats(struct ahci_probe_stats *stats){
    if(stats) *stats=probe_stats;
}
