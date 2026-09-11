#include "ehci.h"

#include "../pci/pci.h"
#include "../storage/storage_probe.h"
#include "../../arch/x86_64/mmio.h"
#include "../../kernel/diagnostics/klog.h"
#include "../../lib/string.h"

#define EHCI_DEVICE_LIMIT 4
#define EHCI_TIMEOUT      50000000U
#define EHCI_MMIO_MAP_SIZE 0x1000U
#define EHCI_NO_DEVICE    0xFF

#define EHCI_CMD_RUN      (1U<<0)
#define EHCI_CMD_RESET    (1U<<1)
#define EHCI_CMD_ASYNC    (1U<<5)
#define EHCI_STS_HALTED   (1U<<12)
#define EHCI_STS_ASYNC    (1U<<15)
#define EHCI_PORT_CONNECT (1U<<0)
#define EHCI_PORT_ENABLE  (1U<<2)
#define EHCI_PORT_RESET   (1U<<8)
#define EHCI_PORT_POWER   (1U<<12)
#define EHCI_PORT_OWNER   (1U<<13)
#define EHCI_PORT_CHANGES ((1U<<1)|(1U<<3)|(1U<<5))

#define EHCI_QTD_ACTIVE   (1U<<7)
#define EHCI_QTD_ERRORS   0x7CU
#define EHCI_PID_OUT      0
#define EHCI_PID_IN       1
#define EHCI_PID_SETUP    2

#define USB_DESCRIPTOR_DEVICE        1
#define USB_DESCRIPTOR_CONFIGURATION 2
#define USB_DESCRIPTOR_INTERFACE     4
#define USB_DESCRIPTOR_ENDPOINT      5
#define USB_CLASS_MASS_STORAGE       8
#define USB_PROTOCOL_BULK_ONLY       0x50
#define USB_CLASS_HID                3
#define USB_CLASS_HUB                9
#define USB_HID_PROTOCOL_MOUSE       2

#define SCSI_TEST_UNIT_READY 0x00
#define SCSI_INQUIRY         0x12
#define SCSI_MODE_SENSE6     0x1A
#define SCSI_READ_CAPACITY10 0x25
#define SCSI_READ10          0x28
#define SCSI_WRITE10         0x2A
#define SCSI_SYNC_CACHE10    0x35

struct ehci_qtd {
    volatile uint32_t next;
    volatile uint32_t alternate;
    volatile uint32_t token;
    volatile uint32_t buffer[5];
    volatile uint32_t extended_buffer[5];
    uint32_t reserved[3];
};

struct ehci_qh {
    volatile uint32_t horizontal;
    volatile uint32_t endpoint_characteristics;
    volatile uint32_t endpoint_capabilities;
    volatile uint32_t current_qtd;
    volatile struct ehci_qtd overlay;
};

struct usb_cbw {
    uint32_t signature;
    uint32_t tag;
    uint32_t transfer_length;
    uint8_t flags;
    uint8_t lun;
    uint8_t command_length;
    uint8_t command[16];
} __attribute__((packed));

struct usb_csw {
    uint32_t signature;
    uint32_t tag;
    uint32_t residue;
    uint8_t status;
} __attribute__((packed));

struct ehci_device {
    struct storage_device_info info;
    uint8_t address;
    uint8_t bulk_in;
    uint8_t bulk_out;
    uint16_t bulk_in_packet;
    uint16_t bulk_out_packet;
    bool bulk_in_toggle;
    bool bulk_out_toggle;
    bool sync_cache_supported;
};

_Static_assert(sizeof(struct usb_cbw)==31,"USB BOT CBW size");
_Static_assert(sizeof(struct usb_csw)==13,"USB BOT CSW size");
_Static_assert(sizeof(struct ehci_qtd)==64,"EHCI qTD stride");
_Static_assert(__builtin_offsetof(struct ehci_qh,overlay)==16,"EHCI QH overlay offset");

static struct ehci_qh queue_head __attribute__((aligned(32)));
static struct ehci_qtd descriptors[3] __attribute__((aligned(32)));
static uint8_t setup_packet[8] __attribute__((aligned(32)));
static uint8_t descriptor_buffer[512] __attribute__((aligned(32)));
static uint8_t transfer_buffer[512] __attribute__((aligned(32)));
static struct usb_cbw command_block __attribute__((aligned(32)));
static struct usb_csw command_status __attribute__((aligned(32)));
static struct ehci_device devices[EHCI_DEVICE_LIMIT];

static volatile uint8_t *capability_base;
static volatile uint32_t *operational;
static uint64_t kernel_physical_base;
static uint64_t kernel_virtual_base;
static uint32_t dma_segment;
static uint32_t bot_tag=1;
static uint8_t controller_number;
static uint8_t device_count;
static uint8_t selected_device=EHCI_NO_DEVICE;
static uint8_t root_port_count;
static bool mapping_ready;
static bool probe_complete;
static uint16_t known_port_bitmap;
static struct ehci_probe_stats probe_stats;

static uint16_t connected_port_bitmap(void){
    if(!operational) return 0;
    uint16_t bitmap=0;
    for(uint8_t port=1;port<=root_port_count;port++){
        if(operational[17+port-1]&EHCI_PORT_CONNECT)
            bitmap|=(uint16_t)(1U<<(port-1));
    }
    return bitmap;
}

bool ehci_topology_changed(void){
    return operational && connected_port_bitmap()!=known_port_bitmap;
}

void ehci_set_address_mapping(uint64_t direct_map_offset, uint64_t physical_base,
                              uint64_t virtual_base){
    (void)direct_map_offset;
    kernel_physical_base=physical_base;
    kernel_virtual_base=virtual_base;
    mapping_ready=true;
}

static uint64_t physical_address(const void *pointer){
    return (uint64_t)(uintptr_t)pointer-kernel_virtual_base+kernel_physical_base;
}

static uint32_t read_be32(const uint8_t *data){
    return ((uint32_t)data[0]<<24)|((uint32_t)data[1]<<16)
        |((uint32_t)data[2]<<8)|data[3];
}

static void write_be32(uint8_t *data, uint32_t value){
    data[0]=(uint8_t)(value>>24);
    data[1]=(uint8_t)(value>>16);
    data[2]=(uint8_t)(value>>8);
    data[3]=(uint8_t)value;
}

static void delay(void){
    for(volatile uint32_t wait=0;wait<1000000;wait++){
        __asm__ volatile("pause");
    }
}

static bool wait_register(volatile uint32_t *reg, uint32_t mask, bool set){
    for(uint32_t wait=0;wait<EHCI_TIMEOUT;wait++){
        if(((*reg&mask)!=0)==set) return true;
        __asm__ volatile("pause");
    }
    return false;
}

static bool same_dma_segment(const void *pointer){
    return (uint32_t)(physical_address(pointer)>>32)==dma_segment;
}

static void set_qtd_buffer(struct ehci_qtd *qtd, const void *buffer){
    uint64_t address=physical_address(buffer);
    qtd->buffer[0]=(uint32_t)address;
    qtd->extended_buffer[0]=(uint32_t)(address>>32);
    uint64_t page=(address&~0xFFFULL)+0x1000;
    for(uint8_t index=1;index<5;index++,page+=0x1000){
        qtd->buffer[index]=(uint32_t)page;
        qtd->extended_buffer[index]=(uint32_t)(page>>32);
    }
}

static void prepare_qtd(struct ehci_qtd *qtd, uint8_t pid, void *buffer,
                        uint32_t length, bool toggle, bool interrupt){
    memset(qtd,0,sizeof(*qtd));
    qtd->next=1;
    qtd->alternate=1;
    qtd->token=(toggle ? 1U<<31 : 0)|(length<<16)
        |(interrupt ? 1U<<15 : 0)|(3U<<10)|((uint32_t)pid<<8)|EHCI_QTD_ACTIVE;
    if(length) set_qtd_buffer(qtd,buffer);
}

static void prepare_queue_head(uint8_t address, uint8_t endpoint,
                               uint16_t max_packet){
    memset(&queue_head,0,sizeof(queue_head));
    uint32_t qh_address=(uint32_t)physical_address(&queue_head);
    queue_head.horizontal=qh_address|(1U<<1);
    queue_head.endpoint_characteristics=(uint32_t)address
        |((uint32_t)endpoint<<8)|(2U<<12)|(1U<<14)|(1U<<15)
        |((uint32_t)max_packet<<16)|(4U<<28);
    queue_head.endpoint_capabilities=1U<<30;
    queue_head.overlay.next=1;
    queue_head.overlay.alternate=1;
}

static bool execute_schedule(struct ehci_qtd *first,
                             struct ehci_qtd *last){
    operational[0]&=~EHCI_CMD_ASYNC;
    if(!wait_register(&operational[1],EHCI_STS_ASYNC,false)) return false;
    queue_head.overlay.next=(uint32_t)physical_address(first);
    queue_head.overlay.alternate=1;
    queue_head.overlay.token=0;
    operational[6]=(uint32_t)physical_address(&queue_head);
    __sync_synchronize();
    operational[1]=0x3F;
    operational[0]|=EHCI_CMD_ASYNC;
    if(!wait_register(&operational[1],EHCI_STS_ASYNC,true)) return false;
    bool completed=false;
    for(uint32_t wait=0;wait<EHCI_TIMEOUT;wait++){
        uint32_t token=last->token;
        if(!(token&EHCI_QTD_ACTIVE)){
            completed=(token&EHCI_QTD_ERRORS)==0;
            break;
        }
        __asm__ volatile("pause");
    }
    operational[0]&=~EHCI_CMD_ASYNC;
    (void)wait_register(&operational[1],EHCI_STS_ASYNC,false);
    return completed;
}

static bool control_transfer(uint8_t address, uint16_t max_packet,
                             uint8_t request_type, uint8_t request,
                             uint16_t value, uint16_t index, void *buffer,
                             uint16_t length, bool data_in){
    setup_packet[0]=request_type;
    setup_packet[1]=request;
    setup_packet[2]=(uint8_t)value;
    setup_packet[3]=(uint8_t)(value>>8);
    setup_packet[4]=(uint8_t)index;
    setup_packet[5]=(uint8_t)(index>>8);
    setup_packet[6]=(uint8_t)length;
    setup_packet[7]=(uint8_t)(length>>8);
    prepare_queue_head(address,0,max_packet);
    prepare_qtd(&descriptors[0],EHCI_PID_SETUP,setup_packet,8,false,false);
    uint8_t last_index=1;
    if(length){
        prepare_qtd(&descriptors[1],data_in ? EHCI_PID_IN : EHCI_PID_OUT,
                    buffer,length,true,false);
        prepare_qtd(&descriptors[2],data_in ? EHCI_PID_OUT : EHCI_PID_IN,
                    0,0,true,true);
        descriptors[0].next=(uint32_t)physical_address(&descriptors[1]);
        descriptors[1].next=(uint32_t)physical_address(&descriptors[2]);
        descriptors[1].alternate=(uint32_t)physical_address(&descriptors[2]);
        last_index=2;
    } else {
        prepare_qtd(&descriptors[1],EHCI_PID_IN,0,0,true,true);
        descriptors[0].next=(uint32_t)physical_address(&descriptors[1]);
    }
    return execute_schedule(&descriptors[0],&descriptors[last_index]);
}

static bool bulk_transfer(uint8_t address, uint8_t endpoint,
                          uint16_t max_packet, void *buffer, uint32_t length,
                          bool input, bool *toggle){
    prepare_queue_head(address,endpoint,max_packet);
    prepare_qtd(&descriptors[0],input ? EHCI_PID_IN : EHCI_PID_OUT,
                buffer,length,*toggle,true);
    if(!execute_schedule(&descriptors[0],&descriptors[0])) return false;
    uint32_t packets=(length+max_packet-1)/max_packet;
    if(packets&1) *toggle=!*toggle;
    return true;
}

static bool get_descriptor(uint8_t address, uint16_t packet_size, uint8_t type,
                           uint16_t length){
    memset(descriptor_buffer,0,sizeof(descriptor_buffer));
    return control_transfer(address,packet_size,0x80,6,(uint16_t)type<<8,0,
                            descriptor_buffer,length,true);
}

static bool reset_port(uint8_t port){
    volatile uint32_t *status=&operational[17+port-1];
    uint32_t value=*status;
    klogf(KLOG_DEBUG,"ehci%u: port%u reset entry PORTSC=0x%08x",controller_number,port,value);
    if(!(value&EHCI_PORT_CONNECT)){
        klogf(KLOG_DEBUG,"ehci%u: port%u no CONNECT",controller_number,port);
        return false;
    }
    klogf(KLOG_INFO,"ehci%u: port%u CONNECT=1 POWER=%u ENABLE=%u OWNER=%u",
          controller_number,port,(value>>12)&1,(value>>2)&1,(value>>13)&1);
    probe_stats.connected_ports++;
    if(!(value&EHCI_PORT_POWER)){
        klogf(KLOG_WARN,"ehci%u: port%u POWER 0 -> set",controller_number,port);
        *status=(value&~EHCI_PORT_CHANGES)|EHCI_PORT_POWER;
        delay();
        value=*status;
        klogf(KLOG_INFO,"ehci%u: port%u after POWER set PORTSC=0x%08x",controller_number,port,value);
    }
    klogf(KLOG_INFO,"ehci%u: port%u issuing RESET",controller_number,port);
    *status=(value&~(EHCI_PORT_CHANGES|EHCI_PORT_ENABLE))|EHCI_PORT_RESET;
    delay();
    value=*status;
    klogf(KLOG_DEBUG,"ehci%u: port%u after RESET pulse PORTSC=0x%08x",controller_number,port,value);
    *status=value&~(EHCI_PORT_CHANGES|EHCI_PORT_RESET|EHCI_PORT_ENABLE);
    delay();
    value=*status;
    klogf(KLOG_INFO,"ehci%u: port%u after clear PORTSC=0x%08x CONNECT=%u ENABLE=%u",controller_number,port,value,value&1,(value>>2)&1);
    if(!(value&EHCI_PORT_CONNECT) || !(value&EHCI_PORT_ENABLE)){
        klogf(KLOG_WARN,"ehci%u: port%u handoff to companion (OWNER=1) PORTSC=0x%08x",controller_number,port,value);
        *status=(value&~EHCI_PORT_CHANGES)|EHCI_PORT_OWNER;
        return false;
    }
    probe_stats.high_speed_ports++;
    klogf(KLOG_OK,"ehci%u: port%u high-speed enabled PORTSC=0x%08x",controller_number,port,value);
    return true;
}

static bool parse_mass_storage(uint16_t total, uint8_t *configuration,
                               uint8_t *bulk_in, uint16_t *in_packet,
                               uint8_t *bulk_out, uint16_t *out_packet){
    bool mass=false;
    for(uint16_t offset=0;offset+2<=total;){
        uint8_t length=descriptor_buffer[offset];
        uint8_t type=descriptor_buffer[offset+1];
        if(length<2 || offset+length>total) return false;
        if(type==USB_DESCRIPTOR_CONFIGURATION && length>=9){
            *configuration=descriptor_buffer[offset+5];
        } else if(type==USB_DESCRIPTOR_INTERFACE && length>=9){
            mass=descriptor_buffer[offset+5]==USB_CLASS_MASS_STORAGE
                && descriptor_buffer[offset+7]==USB_PROTOCOL_BULK_ONLY;
        } else if(type==USB_DESCRIPTOR_ENDPOINT && length>=7 && mass
                  && (descriptor_buffer[offset+3]&3)==2){
            uint8_t endpoint=descriptor_buffer[offset+2];
            uint16_t packet=(uint16_t)descriptor_buffer[offset+4]
                |((uint16_t)descriptor_buffer[offset+5]<<8);
            if(endpoint&0x80){ *bulk_in=endpoint&0x0F; *in_packet=packet; }
            else { *bulk_out=endpoint&0x0F; *out_packet=packet; }
        }
        offset+=length;
    }
    return *configuration && *bulk_in && *bulk_out;
}

static bool detect_hid_mouse(uint16_t total){
    bool is_mouse=false;
    for(uint16_t offset=0;offset+2<=total;){
        uint8_t length=descriptor_buffer[offset];
        uint8_t type=descriptor_buffer[offset+1];
        if(length<2 || offset+length>total) return false;
        if(type==USB_DESCRIPTOR_INTERFACE && length>=9){
            if(descriptor_buffer[offset+5]==USB_CLASS_HID
               && descriptor_buffer[offset+7]==USB_HID_PROTOCOL_MOUSE){
                is_mouse=true;
            }
            if(descriptor_buffer[offset+5]==USB_CLASS_HUB){
                klogf(KLOG_INFO,"ehci%u: hub detected on port (behind hub mouse not supported)",controller_number);
            }
        }
        offset+=length;
    }
    return is_mouse;
}

static bool bulk_only_command(uint8_t index, const uint8_t *command,
                              uint8_t command_length, void *data,
                              uint32_t data_length, bool data_in){
    struct ehci_device *device=&devices[index];
    memset(&command_block,0,sizeof(command_block));
    uint32_t tag=bot_tag++;
    command_block.signature=0x43425355;
    command_block.tag=tag;
    command_block.transfer_length=data_length;
    command_block.flags=data_in ? 0x80 : 0;
    command_block.command_length=command_length;
    memcpy(command_block.command,command,command_length);
    if(!bulk_transfer(device->address,device->bulk_out,device->bulk_out_packet,
                      &command_block,sizeof(command_block),false,
                      &device->bulk_out_toggle)) return false;
    if(data_length){
        uint8_t endpoint=data_in ? device->bulk_in : device->bulk_out;
        uint16_t packet=data_in ? device->bulk_in_packet : device->bulk_out_packet;
        bool *toggle=data_in ? &device->bulk_in_toggle : &device->bulk_out_toggle;
        if(!bulk_transfer(device->address,endpoint,packet,data,data_length,data_in,
                          toggle)){
            return false;
        }
    }
    memset(&command_status,0,sizeof(command_status));
    if(!bulk_transfer(device->address,device->bulk_in,device->bulk_in_packet,
                      &command_status,sizeof(command_status),true,
                      &device->bulk_in_toggle)) return false;
    return command_status.signature==0x53425355
        && command_status.tag==tag && command_status.status==0;
}

static void build_device_name(char output[STORAGE_DEVICE_NAME_CAPACITY],
                              uint32_t index){
    memset(output,0,STORAGE_DEVICE_NAME_CAPACITY);
    memcpy(output,"/dev/sd",7);
    output[7]=(char)('a'+index);
}

static void build_serial(char output[STORAGE_SERIAL_CAPACITY], uint16_t vendor,
                         uint16_t product, uint8_t port){
    const char *hex="0123456789ABCDEF";
    memcpy(output,"USB2-",5);
    for(uint8_t i=0;i<4;i++) output[5+i]=hex[(vendor>>(12-i*4))&0xF];
    output[9]='-';
    for(uint8_t i=0;i<4;i++) output[10+i]=hex[(product>>(12-i*4))&0xF];
    output[14]='-'; output[15]='P'; output[16]=hex[(port>>4)&0xF];
    output[17]=hex[port&0xF]; output[18]='\0';
}

static void copy_model(char output[STORAGE_MODEL_CAPACITY], const uint8_t *data){
    uint8_t length=0;
    for(uint8_t i=8;i<36 && length+1<STORAGE_MODEL_CAPACITY;i++){
        char c=(char)data[i];
        output[length++]=(c>=' ' && c<='~') ? c : ' ';
    }
    while(length && output[length-1]==' ') length--;
    output[length]='\0';
    if(!length) memcpy(output,"USB 2.0 Mass Storage",21);
}

static bool identify_device(uint8_t index, uint32_t name_index, uint8_t port,
                            uint16_t vendor, uint16_t product){
    uint8_t command[16];
    memset(command,0,sizeof(command));
    command[0]=SCSI_TEST_UNIT_READY;
    bool ready=false;
    for(uint8_t attempt=0;attempt<3;attempt++){
        if(bulk_only_command(index,command,6,0,0,true)){ ready=true; break; }
    }
    if(!ready) return false;
    memset(command,0,sizeof(command));
    command[0]=SCSI_INQUIRY;
    command[4]=36;
    if(!bulk_only_command(index,command,6,transfer_buffer,36,true)) return false;

    struct storage_device_info *info=&devices[index].info;
    memset(info,0,sizeof(*info));
    build_device_name(info->name,name_index);
    copy_model(info->model,transfer_buffer);
    build_serial(info->serial,vendor,product,port);
    memset(command,0,sizeof(command));
    command[0]=SCSI_READ_CAPACITY10;
    if(!bulk_only_command(index,command,10,transfer_buffer,8,true)) return false;
    uint32_t last_lba=read_be32(transfer_buffer);
    uint32_t sector_size=read_be32(&transfer_buffer[4]);
    if(last_lba==0xFFFFFFFF || sector_size!=512) return false;
    info->sector_count=(uint64_t)last_lba+1;
    info->sector_size=512;
    info->transport=STORAGE_TRANSPORT_USB_EHCI;
    info->controller=controller_number;
    info->port=port;
    info->operational=1;
    info->writable=1;
    devices[index].sync_cache_supported=true;
    memset(command,0,sizeof(command));
    command[0]=SCSI_MODE_SENSE6;
    command[2]=0x3F;
    command[4]=4;
    if(bulk_only_command(index,command,6,transfer_buffer,4,true)){
        info->writable=(transfer_buffer[2]&0x80)==0;
    }
    return true;
}

static bool enumerate_port(uint8_t port, uint32_t name_index){
    uint8_t index=device_count;
    uint8_t address=(uint8_t)(index+1);
    probe_stats.last_stage=3;
    if(!get_descriptor(0,64,USB_DESCRIPTOR_DEVICE,8)) return false;
    uint16_t packet=descriptor_buffer[7];
    if(!packet) return false;
    probe_stats.last_stage=4;
    if(!control_transfer(0,packet,0,5,address,0,0,0,false)) return false;
    delay();
    if(!get_descriptor(address,packet,USB_DESCRIPTOR_DEVICE,18)) return false;
    uint16_t vendor=(uint16_t)descriptor_buffer[8]
        |((uint16_t)descriptor_buffer[9]<<8);
    uint16_t product=(uint16_t)descriptor_buffer[10]
        |((uint16_t)descriptor_buffer[11]<<8);
    if(!get_descriptor(address,packet,USB_DESCRIPTOR_CONFIGURATION,9)) return false;
    uint16_t total=(uint16_t)descriptor_buffer[2]
        |((uint16_t)descriptor_buffer[3]<<8);
    if(total<9 || total>sizeof(descriptor_buffer)
       || !get_descriptor(address,packet,USB_DESCRIPTOR_CONFIGURATION,total)){
        return false;
    }
    uint8_t configuration=0,bulk_in=0,bulk_out=0;
    uint16_t in_packet=0,out_packet=0;
    if(!parse_mass_storage(total,&configuration,&bulk_in,&in_packet,
                           &bulk_out,&out_packet)){
        if(detect_hid_mouse(total)){
            klogf(KLOG_WARN,"ehci%u: HID boot mouse found on port%u but EHCI HID not implemented – подключите мышь к USB3 (xHCI, синий порт) или используйте PS/2",controller_number,port);
        } else {
            klogf(KLOG_DEBUG,"ehci%u: port%u no mass-storage interface (maybe HID hub)",controller_number,port);
        }
        return false;
    }
    probe_stats.last_stage=5;
    if(!control_transfer(address,packet,0,9,configuration,0,0,0,false)) return false;
    memset(&devices[index],0,sizeof(devices[index]));
    devices[index].address=address;
    devices[index].bulk_in=bulk_in;
    devices[index].bulk_out=bulk_out;
    devices[index].bulk_in_packet=in_packet;
    devices[index].bulk_out_packet=out_packet;
    probe_stats.last_stage=6;
    if(!identify_device(index,name_index,port,vendor,product)) return false;
    device_count++;
    probe_stats.mass_storage_devices++;
    probe_stats.last_stage=7;
    return true;
}

static bool take_ownership(const struct storage_controller_info *controller,
                           uint32_t hccparams){
    uint8_t offset=(uint8_t)(hccparams>>8);
    for(uint8_t visited=0;offset && visited<32;visited++){
        uint32_t legacy=pci_read_config32(controller->bus,controller->slot,
                                          controller->function,offset);
        if((legacy&0xFF)==1){
            pci_write_config32(controller->bus,controller->slot,
                               controller->function,offset,legacy|(1U<<24));
            for(uint32_t wait=0;wait<EHCI_TIMEOUT;wait++){
                legacy=pci_read_config32(controller->bus,controller->slot,
                                         controller->function,offset);
                if(!(legacy&(1U<<16))){
                    if(offset<=0xF8){
                        pci_write_config32(controller->bus,controller->slot,
                                           controller->function,(uint8_t)(offset+4),0);
                    }
                    return true;
                }
            }
            return false;
        }
        offset=(uint8_t)((legacy>>8)&0xFF);
    }
    return true;
}

static bool initialize_controller(const struct storage_controller_info *controller,
                                  uint32_t linux_name_base){
    if(!controller->register_base){
        klogf(KLOG_ERROR,"ehci%u: BAR zero pci %u:%u.%u",controller_number,controller->bus,controller->slot,controller->function);
        return false;
    }
    uint32_t pci_command=pci_read_config32(controller->bus,controller->slot,
                                            controller->function,0x04);
    klogf(KLOG_INFO,"ehci%u: PCI %u:%u.%u BAR=0x%llx CMD=0x%04x",controller_number,controller->bus,controller->slot,controller->function,controller->register_base,pci_command);
    pci_write_config32(controller->bus,controller->slot,controller->function,
                       0x04,pci_command|0x06);
    capability_base=(volatile uint8_t*)mmio_map(controller->register_base,
                                                EHCI_MMIO_MAP_SIZE);
    if(!capability_base){
        klogf(KLOG_ERROR,"ehci%u: cannot map BAR 0x%llx",controller_number,
              controller->register_base);
        return false;
    }
    klogf(KLOG_INFO,"ehci%u: BAR phys=0x%llx mapped=%p",controller_number,
          controller->register_base,(void*)capability_base);
    uint8_t capability_length=capability_base[0];
    volatile uint32_t *capability=(volatile uint32_t*)(void*)capability_base;
    uint32_t structural=capability[1];
    uint32_t capability_parameters=capability[2];
    uint8_t hcs_iv=capability_base[0];
    klogf(KLOG_INFO,"ehci%u: CAPLEN=0x%02x HCSPARAMS=0x%08x HCCPARAMS=0x%08x ports=%u",controller_number,capability_length,structural,capability_parameters,structural&0x0F);
    if(capability_length<0x10){
        klogf(KLOG_ERROR,"ehci%u: caplen too short 0x%02x",controller_number,capability_length);
        return false;
    }
    if(!take_ownership(controller,capability_parameters)){
        klogf(KLOG_ERROR,"ehci%u: BIOS handoff failed",controller_number);
        return false;
    }
    uint8_t ports=(uint8_t)(structural&0x0F);
    if(!ports){
        klogf(KLOG_ERROR,"ehci%u: zero ports structural=0x%08x",controller_number,structural);
        return false;
    }
    root_port_count=ports;
    operational=(volatile uint32_t*)(void*)(capability_base+capability_length);
    klogf(KLOG_INFO,"ehci%u: operational=%p USBCMD=0x%08x USBSTS=0x%08x",controller_number,(void*)operational,operational[0],operational[1]);
    operational[0]&=~EHCI_CMD_RUN;
    if(!wait_register(&operational[1],EHCI_STS_HALTED,true)){
        klogf(KLOG_ERROR,"ehci%u: halt timeout STS=0x%08x",controller_number,operational[1]);
        return false;
    }
    operational[0]|=EHCI_CMD_RESET;
    if(!wait_register(&operational[0],EHCI_CMD_RESET,false)){
        klogf(KLOG_ERROR,"ehci%u: reset timeout",controller_number);
        return false;
    }

    dma_segment=(uint32_t)(physical_address(&queue_head)>>32);
    klogf(KLOG_INFO,"ehci%u: dma_segment=0x%08x cap 64bit=%u",controller_number,dma_segment,(capability_parameters&1)!=0);
    if(!(capability_parameters&1) && dma_segment){
        klogf(KLOG_ERROR,"ehci%u: DMA above 4G unsupported",controller_number);
        return false;
    }
    bool same = same_dma_segment(descriptors)&&same_dma_segment(setup_packet)
       &&same_dma_segment(descriptor_buffer)&&same_dma_segment(transfer_buffer)
       &&same_dma_segment(&command_block)&&same_dma_segment(&command_status);
    if(!same){
        klogf(KLOG_ERROR,"ehci%u: DMA buffers cross 4G segment dma_seg=0x%08x",controller_number,dma_segment);
        klogf(KLOG_INFO,"ehci%u: addrs: qh 0x%llx desc 0x%llx setup 0x%llx buf 0x%llx xfer 0x%llx cbw 0x%llx csw 0x%llx",
              controller_number,physical_address(&queue_head),physical_address(descriptors),physical_address(setup_packet),
              physical_address(descriptor_buffer),physical_address(transfer_buffer),physical_address(&command_block),physical_address(&command_status));
        return false;
    }
    operational[2]=0;
    operational[4]=dma_segment;
    operational[6]=(uint32_t)physical_address(&queue_head);
    operational[16]=1;
    operational[0]=(operational[0]&~(3U<<2))|EHCI_CMD_RUN;
    klogf(KLOG_INFO,"ehci%u: RUN set USBCMD=0x%08x USBSTS=0x%08x",controller_number,operational[0],operational[1]);
    if(!wait_register(&operational[1],EHCI_STS_HALTED,false)){
        klogf(KLOG_ERROR,"ehci%u: RUN timeout STS=0x%08x",controller_number,operational[1]);
        return false;
    }
    probe_stats.last_stage=2;
    // dump all ports raw before probing
    for(uint8_t p=1;p<=ports;p++){
        volatile uint32_t *pp=&operational[17+p-1];
        klogf(KLOG_INFO,"ehci%u: port%u pre-scan PORTSC=0x%08x",controller_number,p,*pp);
    }
    for(uint8_t port=1;port<=ports && device_count<EHCI_DEVICE_LIMIT;port++){
        klogf(KLOG_INFO,"ehci%u: probing port %u",controller_number,port);
        if(!reset_port(port)){
            klogf(KLOG_DEBUG,"ehci%u: port %u reset failed, maybe companion",controller_number,port);
            continue;
        }
        if(!enumerate_port(port,linux_name_base+device_count)){
            probe_stats.failures++;
            klogf(KLOG_ERROR,"ehci%u: port %u enumerate failed",controller_number,port);
        } else {
            klogf(KLOG_OK,"ehci%u: port %u MSC ready disks=%u",controller_number,port,probe_stats.mass_storage_devices);
        }
    }
    // post-scan dump
    for(uint8_t p=1;p<=ports;p++){
        volatile uint32_t *pp=&operational[17+p-1];
        klogf(KLOG_DEBUG,"ehci%u: port%u post-scan PORTSC=0x%08x",controller_number,p,*pp);
    }
    klogf(KLOG_INFO,"ehci%u: scan done connected=%u highspeed=%u disks=%u failures=%u",controller_number,probe_stats.connected_ports,probe_stats.high_speed_ports,probe_stats.mass_storage_devices,probe_stats.failures);
    return true;
}

bool ehci_init(uint32_t linux_name_base){
    if(probe_complete){
        return device_count>0;
    }
    probe_complete=true;
    if(!mapping_ready){
        klog(KLOG_ERROR,"ehci: mapping not ready");
        return false;
    }
    bool was_screen=klog_is_screen_enabled();
    klog_set_screen_enabled(false);
    struct storage_controller_info controllers[8];
    int32_t count=storage_controller_list(controllers,8);
    if(count<0){
        klog_set_screen_enabled(was_screen);
        return false;
    }
    uint8_t ehci_index=0;
    for(int32_t index=0;index<count;index++){
        if(controllers[index].type!=STORAGE_CONTROLLER_EHCI) continue;
        probe_stats.controllers++;
        probe_stats.last_stage=1;
        controller_number=ehci_index++;
        if(!initialize_controller(&controllers[index],linux_name_base)){
            probe_stats.failures++;
            continue;
        }
        if(device_count) break;
    }
    klog_set_screen_enabled(was_screen);
    if(probe_stats.controllers==0){
        klog(KLOG_INFO,"ehci: no controllers");
    } else {
        klogf(KLOG_INFO,"ehci: controllers=%u disks=%u",probe_stats.controllers,device_count);
    }
    known_port_bitmap=connected_port_bitmap();
    return device_count>0;
}

bool ehci_rescan(uint32_t linux_name_base){
    if(!mapping_ready){
        klog(KLOG_ERROR,"ehci: rescan mapping not ready");
        return false;
    }
    selected_device=EHCI_NO_DEVICE;
    device_count=0;
    memset(devices,0,sizeof(devices));
    probe_stats.connected_ports=0;
    probe_stats.high_speed_ports=0;
    probe_stats.mass_storage_devices=0;
    probe_stats.failures=0;
    probe_stats.last_stage=0;

    bool was_screen=klog_is_screen_enabled();
    klog_set_screen_enabled(false);
    struct storage_controller_info controllers[8];
    int32_t count=storage_controller_list(controllers,8);
    if(count<0){
        klog_set_screen_enabled(was_screen);
        return false;
    }
    uint8_t ehci_index=0;
    for(int32_t index=0;index<count;index++){
        if(controllers[index].type!=STORAGE_CONTROLLER_EHCI) continue;
        probe_stats.last_stage=1;
        controller_number=ehci_index++;
        if(!initialize_controller(&controllers[index],linux_name_base)){
            probe_stats.failures++;
            continue;
        }
        if(device_count) break;
    }
    klog_set_screen_enabled(was_screen);
    known_port_bitmap=connected_port_bitmap();
    klogf(KLOG_INFO,"ehci: rescan done disks=%u connected=%u",device_count,probe_stats.connected_ports);
    return device_count>0;
}

uint32_t ehci_device_count(void){ return device_count; }

bool ehci_get_device_info(uint32_t index, struct storage_device_info *info){
    if(!info || index>=device_count) return false;
    *info=devices[index].info;
    return true;
}

bool ehci_select_device(uint32_t index){
    if(index>=device_count) return false;
    selected_device=(uint8_t)index;
    return true;
}

static bool sector_command(uint8_t operation, uint32_t lba, void *buffer,
                           bool input){
    if(selected_device>=device_count || !buffer
       || lba>=devices[selected_device].info.sector_count) return false;
    uint8_t command[16];
    memset(command,0,sizeof(command));
    command[0]=operation;
    write_be32(&command[2],lba);
    command[8]=1;
    return bulk_only_command(selected_device,command,10,buffer,512,input);
}

bool ehci_read_sector(uint32_t lba, void *buffer){
    return sector_command(SCSI_READ10,lba,buffer,true);
}

bool ehci_write_sector(uint32_t lba, const void *buffer){
    if(selected_device>=device_count || !devices[selected_device].info.writable){
        return false;
    }
    if(!sector_command(SCSI_WRITE10,lba,(void*)buffer,false)) return false;
    struct ehci_device *device=&devices[selected_device];
    if(!device->sync_cache_supported) return true;
    uint8_t command[16];
    memset(command,0,sizeof(command));
    command[0]=SCSI_SYNC_CACHE10;
    if(bulk_only_command(selected_device,command,10,0,0,false)) return true;
    device->sync_cache_supported=false;
    klogf(KLOG_WARN,"ehci%u: dev %s rejected SYNCHRONIZE CACHE; WRITE(10) succeeded",
          controller_number,device->info.name);
    return true;
}

const char *ehci_device_name(void){
    return selected_device<device_count ? devices[selected_device].info.name : "none";
}

void ehci_get_probe_stats(struct ehci_probe_stats *stats){
    if(stats) *stats=probe_stats;
}
