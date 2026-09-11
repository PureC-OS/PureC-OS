#include "ata_pio.h"

#include "../../lib/string.h"

#define ATA_REG_DATA       0
#define ATA_REG_ERROR      1
#define ATA_REG_FEATURES   1
#define ATA_REG_SECCOUNT   2
#define ATA_REG_LBA_LOW    3
#define ATA_REG_LBA_MID    4
#define ATA_REG_LBA_HIGH   5
#define ATA_REG_DRIVE      6
#define ATA_REG_STATUS     7
#define ATA_REG_COMMAND    7

#define ATA_STATUS_ERROR   0x01
#define ATA_STATUS_DRQ     0x08
#define ATA_STATUS_FAULT   0x20
#define ATA_STATUS_BUSY    0x80

#define ATA_CMD_READ       0x20
#define ATA_CMD_WRITE      0x30
#define ATA_CMD_FLUSH      0xE7
#define ATA_CMD_IDENTIFY   0xEC
#define ATA_POLL_LIMIT     1000000
#define ATA_DEVICE_LIMIT   4
#define ATA_LBA28_SECTORS  0x10000000U
#define ATA_NO_DEVICE      0xFF

struct ata_device {
    uint16_t io_base;
    uint16_t control_base;
    uint32_t addressable_sectors;
    struct storage_device_info info;
    bool available;
};

struct ata_candidate {
    uint16_t io_base;
    uint16_t control_base;
    uint8_t channel;
    uint8_t drive;
};

static struct ata_device devices[ATA_DEVICE_LIMIT];
static uint8_t device_count;
static uint8_t selected_device=ATA_NO_DEVICE;
static bool probe_complete;

static inline void outb(uint16_t port, uint8_t value){
    __asm__ volatile("outb %0,%1"::"a"(value),"Nd"(port));
}

static inline uint8_t inb(uint16_t port){
    uint8_t value;
    __asm__ volatile("inb %1,%0":"=a"(value):"Nd"(port));
    return value;
}

static inline uint16_t inw(uint16_t port){
    uint16_t value;
    __asm__ volatile("inw %1,%0":"=a"(value):"Nd"(port));
    return value;
}

static inline void outw(uint16_t port, uint16_t value){
    __asm__ volatile("outw %0,%1"::"a"(value),"Nd"(port));
}

static void delay_400ns(const struct ata_device *device){
    for(uint8_t index=0;index<4;index++) (void)inb(device->control_base);
}

static bool wait_not_busy(const struct ata_device *device){
    for(uint32_t attempt=0;attempt<ATA_POLL_LIMIT;attempt++){
        uint8_t status=inb(device->io_base+ATA_REG_STATUS);
        if(status==0xFF) return false;
        if(!(status&ATA_STATUS_BUSY)) return true;
    }
    return false;
}

static bool wait_for_data(const struct ata_device *device){
    for(uint32_t attempt=0;attempt<ATA_POLL_LIMIT;attempt++){
        uint8_t status=inb(device->io_base+ATA_REG_STATUS);
        if(status==0 || status==0xFF) return false;
        if(status&(ATA_STATUS_ERROR|ATA_STATUS_FAULT)) return false;
        if(!(status&ATA_STATUS_BUSY) && (status&ATA_STATUS_DRQ)) return true;
    }
    return false;
}

static bool wait_for_completion(const struct ata_device *device){
    if(!wait_not_busy(device)) return false;
    uint8_t status=inb(device->io_base+ATA_REG_STATUS);
    return status!=0 && status!=0xFF
        && !(status&(ATA_STATUS_ERROR|ATA_STATUS_FAULT));
}

static void copy_identify_text(char *output, uint32_t capacity,
                               const uint16_t identify_data[256],
                               uint16_t first_word, uint16_t word_count){
    uint32_t length=(uint32_t)word_count*2;
    if(length>=capacity) length=capacity-1;
    for(uint32_t index=0;index<length;index++){
        uint16_t word=identify_data[first_word+index/2];
        output[index]=(char)(index&1 ? word&0xFF : word>>8);
    }
    while(length>0 && output[length-1]==' ') length--;
    output[length]='\0';
}

static void set_linux_name(struct storage_device_info *info, uint8_t index){
    info->name[0]='/';
    info->name[1]='d';
    info->name[2]='e';
    info->name[3]='v';
    info->name[4]='/';
    info->name[5]='s';
    info->name[6]='d';
    info->name[7]=(char)('a'+index);
    info->name[8]='\0';
}

static bool identify_device(struct ata_device *device){
    outb(device->control_base,2);
    outb(device->io_base+ATA_REG_DRIVE,device->info.drive ? 0xB0 : 0xA0);
    delay_400ns(device);
    outb(device->io_base+ATA_REG_SECCOUNT,0);
    outb(device->io_base+ATA_REG_LBA_LOW,0);
    outb(device->io_base+ATA_REG_LBA_MID,0);
    outb(device->io_base+ATA_REG_LBA_HIGH,0);
    outb(device->io_base+ATA_REG_COMMAND,ATA_CMD_IDENTIFY);

    uint8_t status=inb(device->io_base+ATA_REG_STATUS);
    if(status==0 || status==0xFF) return false;
    if(!wait_not_busy(device)) return false;
    if(inb(device->io_base+ATA_REG_LBA_MID)!=0
       || inb(device->io_base+ATA_REG_LBA_HIGH)!=0){
        return false;
    }
    if(!wait_for_data(device)) return false;

    uint16_t identify_data[256];
    for(uint16_t word=0;word<256;word++) identify_data[word]=inw(device->io_base+ATA_REG_DATA);
    if(!(identify_data[49]&(1<<9))) return false;

    uint64_t sectors=(uint32_t)identify_data[60]|((uint32_t)identify_data[61]<<16);
    if((identify_data[83]&(1<<10))!=0){
        uint64_t lba48=(uint64_t)identify_data[100]
            |((uint64_t)identify_data[101]<<16)
            |((uint64_t)identify_data[102]<<32)
            |((uint64_t)identify_data[103]<<48);
        if(lba48) sectors=lba48;
    }
    if(sectors==0) return false;

    device->addressable_sectors=sectors>ATA_LBA28_SECTORS
        ? ATA_LBA28_SECTORS : (uint32_t)sectors;
    device->info.sector_count=sectors;
    device->info.sector_size=512;
    device->info.writable=1;
    device->info.transport=STORAGE_TRANSPORT_ATA_PIO;
    device->info.controller=device->info.channel;
    device->info.port=device->info.drive;
    device->info.operational=1;
    copy_identify_text(device->info.serial,STORAGE_SERIAL_CAPACITY,
                       identify_data,10,10);
    copy_identify_text(device->info.model,STORAGE_MODEL_CAPACITY,
                       identify_data,27,20);
    device->available=true;
    return true;
}

bool ata_pio_init(void){
    static const struct ata_candidate candidates[ATA_DEVICE_LIMIT]={
        {0x1F0,0x3F6,0,0},
        {0x1F0,0x3F6,0,1},
        {0x170,0x376,1,0},
        {0x170,0x376,1,1}
    };

    if(probe_complete) return device_count>0;
    probe_complete=true;
    for(uint8_t index=0;index<ATA_DEVICE_LIMIT;index++){
        struct ata_device candidate;
        memset(&candidate,0,sizeof(candidate));
        candidate.io_base=candidates[index].io_base;
        candidate.control_base=candidates[index].control_base;
        candidate.info.channel=candidates[index].channel;
        candidate.info.drive=candidates[index].drive;
        if(!identify_device(&candidate)) continue;

        set_linux_name(&candidate.info,device_count);
        devices[device_count]=candidate;
        device_count++;
    }
    if(device_count>0) selected_device=0;
    return device_count>0;
}

uint32_t ata_pio_device_count(void){
    (void)ata_pio_init();
    return device_count;
}

bool ata_pio_get_device_info(uint32_t index, struct storage_device_info *info){
    if(!info || index>=device_count) return false;
    *info=devices[index].info;
    info->selected=index==selected_device;
    return true;
}

bool ata_pio_select_device(uint32_t index){
    if(index>=device_count) return false;
    selected_device=(uint8_t)index;
    return true;
}

static bool select_lba(uint32_t lba, uint8_t command){
    if(selected_device>=device_count) return false;
    struct ata_device *device=&devices[selected_device];
    if(!device->available || lba>=device->addressable_sectors) return false;
    if(!wait_not_busy(device)) return false;

    outb(device->io_base+ATA_REG_DRIVE,
         (uint8_t)(0xE0|(device->info.drive<<4)|((lba>>24)&0x0F)));
    delay_400ns(device);
    outb(device->io_base+ATA_REG_FEATURES,0);
    outb(device->io_base+ATA_REG_SECCOUNT,1);
    outb(device->io_base+ATA_REG_LBA_LOW,(uint8_t)lba);
    outb(device->io_base+ATA_REG_LBA_MID,(uint8_t)(lba>>8));
    outb(device->io_base+ATA_REG_LBA_HIGH,(uint8_t)(lba>>16));
    outb(device->io_base+ATA_REG_COMMAND,command);
    return command==ATA_CMD_READ ? wait_for_data(device) : wait_not_busy(device);
}

bool ata_pio_read_sector(uint32_t lba, void *buffer){
    if(!buffer || !select_lba(lba,ATA_CMD_READ)) return false;
    struct ata_device *device=&devices[selected_device];
    uint16_t *words=(uint16_t*)buffer;
    for(uint16_t index=0;index<256;index++) words[index]=inw(device->io_base+ATA_REG_DATA);
    delay_400ns(device);
    return true;
}

bool ata_pio_write_sector(uint32_t lba, const void *buffer){
    if(!buffer || !select_lba(lba,ATA_CMD_WRITE)) return false;
    struct ata_device *device=&devices[selected_device];
    if(!wait_for_data(device)) return false;
    const uint16_t *words=(const uint16_t*)buffer;
    for(uint16_t index=0;index<256;index++) outw(device->io_base+ATA_REG_DATA,words[index]);
    return wait_for_completion(device);
}

bool ata_pio_flush(void){
    if(selected_device>=device_count) return false;
    struct ata_device *device=&devices[selected_device];
    if(!wait_not_busy(device)) return false;
    outb(device->io_base+ATA_REG_COMMAND,ATA_CMD_FLUSH);
    return wait_for_completion(device);
}

const char *ata_pio_device_name(void){
    return selected_device<device_count ? devices[selected_device].info.name : "none";
}
