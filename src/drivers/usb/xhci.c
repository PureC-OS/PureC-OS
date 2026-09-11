#include "xhci.h"

#include "../pci/pci.h"
#include "../storage/storage_probe.h"
#include "../../arch/x86_64/mmio.h"
#include "../../kernel/diagnostics/klog.h"
#include "../../lib/string.h"
#include "../mouse/usb_mouse.h"

#define XHCI_DEVICE_LIMIT       4
#define XHCI_RING_ENTRIES       64
#define XHCI_EVENT_ENTRIES      128
#define XHCI_TIMEOUT            200000000U
#define XHCI_MMIO_MAP_SIZE      0x100000U
#define XHCI_SCRATCHPAD_LIMIT   1023
#define XHCI_NO_DEVICE          0xFF

#define XHCI_CMD_RUN            (1U<<0)
#define XHCI_CMD_RESET          (1U<<1)
#define XHCI_STS_HALTED         (1U<<0)
#define XHCI_STS_NOT_READY      (1U<<11)
#define XHCI_PORT_CONNECTED     (1U<<0)
#define XHCI_PORT_ENABLED       (1U<<1)
#define XHCI_PORT_RESET         (1U<<4)
#define XHCI_PORT_POWER         (1U<<9)
#define XHCI_PORT_SPEED_SHIFT   10
#define XHCI_PORT_CHANGE_BITS   (0x7FU<<17)

#define XHCI_TRB_NORMAL         1
#define XHCI_TRB_SETUP          2
#define XHCI_TRB_DATA           3
#define XHCI_TRB_STATUS         4
#define XHCI_TRB_LINK           6
#define XHCI_TRB_ENABLE_SLOT    9
#define XHCI_TRB_ADDRESS_DEVICE 11
#define XHCI_TRB_CONFIGURE_EP   12
#define XHCI_TRB_EVALUATE_CTX   13
#define XHCI_EVENT_TRANSFER     32
#define XHCI_EVENT_COMMAND      33
#define XHCI_COMPLETION_SUCCESS 1
#define XHCI_COMPLETION_SHORT   13

#define USB_DESCRIPTOR_DEVICE       1
#define USB_DESCRIPTOR_CONFIGURATION 2
#define USB_DESCRIPTOR_INTERFACE    4
#define USB_DESCRIPTOR_ENDPOINT     5
#define USB_CLASS_MASS_STORAGE      8
#define USB_PROTOCOL_BULK_ONLY      0x50
#define USB_CLASS_HID               3
#define USB_CLASS_HUB               9
#define USB_HID_SUBCLASS_BOOT       1
#define USB_HID_PROTOCOL_MOUSE      2

#define XHCI_DEVICE_DISABLED        0
#define XHCI_DEVICE_STORAGE         1
#define XHCI_DEVICE_MOUSE           2

#define SCSI_TEST_UNIT_READY 0x00
#define SCSI_INQUIRY         0x12
#define SCSI_MODE_SENSE6     0x1A
#define SCSI_READ_CAPACITY10 0x25
#define SCSI_READ10          0x28
#define SCSI_WRITE10         0x2A
#define SCSI_SYNC_CACHE10    0x35

struct xhci_trb {
    uint32_t parameter_low;
    uint32_t parameter_high;
    uint32_t status;
    uint32_t control;
};

struct xhci_erst_entry {
    uint32_t base_low;
    uint32_t base_high;
    uint32_t size;
    uint32_t reserved;
};

struct xhci_ring_state {
    struct xhci_trb *trbs;
    uint16_t enqueue;
    uint8_t cycle;
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

struct xhci_device {
    struct storage_device_info info;
    uint8_t slot_id;
    uint8_t bulk_in_dci;
    uint8_t bulk_out_dci;
    uint16_t bulk_in_packet;
    uint16_t bulk_out_packet;
    uint8_t kind;
    uint8_t interrupt_in_dci;
    uint16_t interrupt_packet;
    uint8_t interrupt_errors;
    bool interrupt_pending;
    bool sync_cache_supported;
};

_Static_assert(sizeof(struct xhci_trb)==16,"xHCI TRB size");
_Static_assert(sizeof(struct xhci_erst_entry)==16,"xHCI ERST entry size");
_Static_assert(sizeof(struct usb_cbw)==31,"USB BOT CBW size");
_Static_assert(sizeof(struct usb_csw)==13,"USB BOT CSW size");

static uint64_t device_context_base[256] __attribute__((aligned(64)));
static struct xhci_trb command_trbs[XHCI_RING_ENTRIES] __attribute__((aligned(64)));
static struct xhci_trb event_trbs[XHCI_EVENT_ENTRIES] __attribute__((aligned(64)));
static struct xhci_erst_entry event_segment __attribute__((aligned(64)));
static uint64_t scratchpad_pointers[XHCI_SCRATCHPAD_LIMIT]
    __attribute__((aligned(65536)));
static uint8_t scratchpad_pages[XHCI_SCRATCHPAD_LIMIT][4096] __attribute__((aligned(4096)));
static uint8_t input_contexts[XHCI_DEVICE_LIMIT][4096] __attribute__((aligned(64)));
static uint8_t output_contexts[XHCI_DEVICE_LIMIT][4096] __attribute__((aligned(64)));
static struct xhci_trb control_trbs[XHCI_DEVICE_LIMIT][XHCI_RING_ENTRIES]
    __attribute__((aligned(64)));
static struct xhci_trb bulk_in_trbs[XHCI_DEVICE_LIMIT][XHCI_RING_ENTRIES]
    __attribute__((aligned(64)));
static struct xhci_trb bulk_out_trbs[XHCI_DEVICE_LIMIT][XHCI_RING_ENTRIES]
    __attribute__((aligned(64)));
static uint8_t descriptor_buffer[512] __attribute__((aligned(64)));
static uint8_t transfer_buffer[512] __attribute__((aligned(64)));
static uint8_t mouse_reports[XHCI_DEVICE_LIMIT][8] __attribute__((aligned(64)));
static struct usb_cbw command_block __attribute__((aligned(64)));
static struct usb_csw command_status __attribute__((aligned(64)));

static struct xhci_device devices[XHCI_DEVICE_LIMIT];
static struct xhci_ring_state command_ring;
static struct xhci_ring_state control_rings[XHCI_DEVICE_LIMIT];
static struct xhci_ring_state bulk_in_rings[XHCI_DEVICE_LIMIT];
static struct xhci_ring_state bulk_out_rings[XHCI_DEVICE_LIMIT];
static volatile uint8_t *mmio_base;
static volatile uint32_t *operational;
static volatile uint32_t *doorbells;
static volatile uint32_t *runtime;
static uint64_t kernel_physical_base;
static uint64_t kernel_virtual_base;
static uint16_t event_index;
static uint8_t event_cycle;
static uint8_t context_size;
static uint8_t controller_number;
static uint8_t device_count;
static uint8_t slot_count;
static uint8_t selected_device=XHCI_NO_DEVICE;
static uint32_t bot_tag=1;
static bool mapping_ready;
static bool probe_complete;
static uint64_t known_port_bitmap;
static struct xhci_probe_stats probe_stats;

static uint64_t connected_port_bitmap(void){
    if(!operational) return 0;
    uint8_t ports=probe_stats.max_ports>64 ? 64
        : (uint8_t)probe_stats.max_ports;
    uint64_t bitmap=0;
    volatile uint8_t *port_base=(volatile uint8_t*)(void*)operational+0x400;
    for(uint8_t port=1;port<=ports;port++){
        volatile uint32_t *status=(volatile uint32_t*)(void*)
            (port_base+(port-1)*0x10);
        if(status[0]&XHCI_PORT_CONNECTED) bitmap|=1ULL<<(port-1);
    }
    return bitmap;
}

bool xhci_topology_changed(void){
    return operational && connected_port_bitmap()!=known_port_bitmap;
}

static inline uint64_t rdtsc(void){
    uint32_t low,high;
    __asm__ volatile("rdtsc":"=a"(low),"=d"(high));
    return ((uint64_t)high<<32)|low;
}

static uint32_t tsc_mhz(void){
    uint32_t eax,ebx,ecx,edx;
    __asm__ volatile("cpuid":"=a"(eax),"=b"(ebx),"=c"(ecx),"=d"(edx)
                     :"a"(0),"c"(0));
    if(eax>=0x16){
        __asm__ volatile("cpuid":"=a"(eax),"=b"(ebx),"=c"(ecx),"=d"(edx)
                         :"a"(0x16),"c"(0));
        if(eax>=100 && eax<=10000) return eax;
    }
    return 5000;
}

static void delay_ms(uint32_t milliseconds){
    uint64_t deadline=rdtsc()+(uint64_t)tsc_mhz()*1000ULL*milliseconds;
    while((int64_t)(rdtsc()-deadline)<0) __asm__ volatile("pause");
}

void xhci_set_address_mapping(uint64_t direct_map_offset, uint64_t physical_base,
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

static uint32_t *context_at(uint8_t *base, uint8_t index){
    return (uint32_t*)(void*)(base+(uint32_t)index*context_size);
}

static void initialize_ring(struct xhci_ring_state *ring,
                            struct xhci_trb *storage){
    memset(storage,0,sizeof(struct xhci_trb)*XHCI_RING_ENTRIES);
    ring->trbs=storage;
    ring->enqueue=0;
    ring->cycle=1;
}

static struct xhci_trb *ring_next(struct xhci_ring_state *ring){
    if(ring->enqueue==XHCI_RING_ENTRIES-1){
        uint64_t base=physical_address(ring->trbs);
        struct xhci_trb *link=&ring->trbs[ring->enqueue];
        memset(link,0,sizeof(*link));
        link->parameter_low=(uint32_t)base;
        link->parameter_high=(uint32_t)(base>>32);
        link->control=(XHCI_TRB_LINK<<10)|(1U<<1)|ring->cycle;
        __sync_synchronize();
        ring->enqueue=0;
        ring->cycle^=1;
    }
    struct xhci_trb *trb=&ring->trbs[ring->enqueue++];
    memset(trb,0,sizeof(*trb));
    return trb;
}

static bool wait_register(volatile uint32_t *reg, uint32_t mask, bool set){
    for(uint32_t wait=0;wait<XHCI_TIMEOUT;wait++){
        if(((*reg&mask)!=0)==set) return true;
        __asm__ volatile("pause");
    }
    return false;
}

static bool dispatch_mouse_event(const struct xhci_trb *event){
    uint8_t slot=(uint8_t)(event->control>>24);
    uint8_t endpoint=(uint8_t)((event->control>>16)&0x1F);
    for(uint8_t index=0;index<slot_count;index++){
        struct xhci_device *device=&devices[index];
        if(device->kind!=XHCI_DEVICE_MOUSE || device->slot_id!=slot
           || device->interrupt_in_dci!=endpoint) continue;
        device->interrupt_pending=false;
        uint8_t code=(uint8_t)(event->status>>24);
        uint32_t requested=device->interrupt_packet;
        if(requested>sizeof(mouse_reports[index])) requested=sizeof(mouse_reports[index]);
        uint32_t remaining=event->status&0xFFFFFF;
        uint32_t received=remaining<requested ? requested-remaining : requested;
        if(code==XHCI_COMPLETION_SUCCESS || code==XHCI_COMPLETION_SHORT){
            device->interrupt_errors=0;
            if(received>=3) usb_mouse_report(mouse_reports[index],received);
        } else {
            probe_stats.mouse_transfer_errors++;
            probe_stats.last_completion_code=code;
            if(device->interrupt_errors<UINT8_MAX) device->interrupt_errors++;
            if(device->interrupt_errors==1 || device->interrupt_errors==3)
                klogf(KLOG_WARN,"xhci%u: HID mouse transfer error slot=%u ep=%u code=%u; retrying",
                      controller_number,slot,endpoint,code);
        }
        return true;
    }
    return false;
}

static bool next_event(uint8_t wanted_type, uint8_t slot, uint8_t endpoint,
                       struct xhci_trb *result){
    volatile uint32_t *interrupter=runtime+0x20/4;
    for(uint32_t wait=0;wait<XHCI_TIMEOUT;wait++){
        volatile struct xhci_trb *event=&event_trbs[event_index];
        if((event->control&1)==event_cycle){
            __sync_synchronize();
            struct xhci_trb copy=*event;
            uint32_t ev_ctrl = copy.control;
            uint32_t ev_status = copy.status;
            uint8_t type=(uint8_t)((ev_ctrl>>10)&0x3F);
            uint8_t event_slot=(uint8_t)(ev_ctrl>>24);
            uint8_t event_endpoint=(uint8_t)((ev_ctrl>>16)&0x1F);
            uint8_t code=(uint8_t)(ev_status>>24);
            event_index++;
            if(event_index==XHCI_EVENT_ENTRIES){
                event_index=0;
                event_cycle^=1;
            }
            uint64_t dequeue=physical_address(&event_trbs[event_index]);
            interrupter[6]=(uint32_t)dequeue|8;
            interrupter[7]=(uint32_t)(dequeue>>32);
            if(type==XHCI_EVENT_TRANSFER
               && dispatch_mouse_event(&copy)
               && (type!=wanted_type || (slot && event_slot!=slot)
                   || (endpoint && event_endpoint!=endpoint))){
                continue;
            }
            // raw event скрыт от экрана загрузки, виден в dmesg, но не флудит на GOP
            if(type==wanted_type && (!slot || event_slot==slot)
               && (!endpoint || event_endpoint==endpoint)){
                if(result) *result=copy;
                probe_stats.last_completion_code=code;
                bool success=code==XHCI_COMPLETION_SUCCESS
                    || (wanted_type==XHCI_EVENT_TRANSFER
                        && code==XHCI_COMPLETION_SHORT);
                if(!success){
                    probe_stats.last_error=XHCI_PROBE_COMPLETION;
                    // error всегда виден и в dmesg и на экране (кратко)
                    klogf(KLOG_ERROR,"xhci%u: event error type=%u slot=%u ep=%u code=%u",controller_number,type,event_slot,event_endpoint,code);
                }
                return success;
            }
            // несовпадающие события просто пропускаем без лога (экономим ring 32K и GOP перерисовку)
        } else {
            __asm__ volatile("pause");
        }
    }
    probe_stats.last_error=XHCI_PROBE_EVENT_TIMEOUT;
    klogf(KLOG_ERROR,"xhci%u: event timeout wanted=%u slot=%u ep=%u idx=%u cycle=%u usbsts=0x%08x",
          controller_number,wanted_type,slot,endpoint,event_index,event_cycle,operational?operational[1]:0xFFFFFFFF);
    return false;
}

static bool submit_command(uint64_t parameter, uint32_t type, uint8_t slot,
                           uint8_t *result_slot){
    struct xhci_trb *trb=ring_next(&command_ring);
    trb->parameter_low=(uint32_t)parameter;
    trb->parameter_high=(uint32_t)(parameter>>32);
    trb->control=(type<<10)|((uint32_t)slot<<24)|command_ring.cycle;
    __sync_synchronize();
    doorbells[0]=0;
    struct xhci_trb event;
    if(!next_event(XHCI_EVENT_COMMAND,0,0,&event)){
        klogf(KLOG_ERROR,"xhci%u: CMD type=%u slot=%u timeout",controller_number,type,slot);
        return false;
    }
    if(result_slot) *result_slot=(uint8_t)(event.control>>24);
    return true;
}

static bool transfer(uint8_t device_index, uint8_t endpoint,
                     struct xhci_ring_state *ring, void *buffer,
                     uint32_t length){
    struct xhci_trb *trb=ring_next(ring);
    uint64_t address=physical_address(buffer);
    trb->parameter_low=(uint32_t)address;
    trb->parameter_high=(uint32_t)(address>>32);
    trb->status=length;
    trb->control=(XHCI_TRB_NORMAL<<10)|(1U<<5)|ring->cycle;
    __sync_synchronize();
    uint8_t slot=devices[device_index].slot_id;
    doorbells[slot]=endpoint;
    bool ok=next_event(XHCI_EVENT_TRANSFER,slot,endpoint,0);
    if(!ok) klogf(KLOG_ERROR,"xhci%u: transfer failed dev=%u ep=%u len=%u",controller_number,device_index,endpoint,length);
    return ok;
}

static bool control_transfer(uint8_t device_index, uint8_t request_type,
                             uint8_t request, uint16_t value, uint16_t index,
                             void *buffer, uint16_t length, bool data_in){
    struct xhci_device *device=&devices[device_index];
    struct xhci_ring_state *ring=&control_rings[device_index];
    uint64_t setup=(uint64_t)request_type|((uint64_t)request<<8)
        |((uint64_t)value<<16)|((uint64_t)index<<32)|((uint64_t)length<<48);
    struct xhci_trb *trb=ring_next(ring);
    trb->parameter_low=(uint32_t)setup;
    trb->parameter_high=(uint32_t)(setup>>32);
    trb->status=8;
    uint32_t transfer_type=length ? (data_in ? 3U : 2U) : 0U;
    trb->control=(XHCI_TRB_SETUP<<10)|(1U<<6)|(transfer_type<<16)|ring->cycle;
    if(length){
        trb=ring_next(ring);
        uint64_t address=physical_address(buffer);
        trb->parameter_low=(uint32_t)address;
        trb->parameter_high=(uint32_t)(address>>32);
        trb->status=length;
        trb->control=(XHCI_TRB_DATA<<10)|(data_in ? 1U<<16 : 0)|ring->cycle;
    }
    trb=ring_next(ring);
    bool status_in=length ? !data_in : true;
    trb->control=(XHCI_TRB_STATUS<<10)|(status_in ? 1U<<16 : 0)
        |(1U<<5)|ring->cycle;
    __sync_synchronize();
    doorbells[device->slot_id]=1;
    return next_event(XHCI_EVENT_TRANSFER,device->slot_id,1,0);
}

static uint16_t initial_packet_size(uint8_t speed){
    if(speed>=4) return 512;
    if(speed==3) return 64;
    return 8;
}

static uint8_t fs_ls_interrupt_interval(uint8_t b_interval){
    uint32_t microframes=(uint32_t)(b_interval ? b_interval : 1U)*8U;
    uint32_t period=1;
    uint8_t exponent=0;
    while(exponent<10 && period<=microframes/2U){
        period<<=1;
        exponent++;
    }
    if(exponent<3) exponent=3;
    return exponent;
}

static void xhci_log_port(uint8_t port_number, uint32_t portsc, const char *when){
    uint8_t ccs=(portsc&1)!=0;
    uint8_t ped=(portsc>>1)&1;
    uint8_t pr=(portsc>>4)&1;
    uint8_t pp=(portsc>>9)&1;
    uint8_t speed=(portsc>>10)&0xF;
    uint8_t csc=(portsc>>17)&1;
    uint8_t pec=(portsc>>18)&1;
    uint8_t wrc=(portsc>>19)&1;
    uint8_t occ=(portsc>>20)&1;
    uint8_t prc=(portsc>>21)&1;
    uint8_t plc=(portsc>>22)&1;
    uint8_t cec=(portsc>>23)&1;
    uint8_t cas=(portsc>>24)&1;
    klogf(KLOG_INFO,"xhci%u: port%u %s PORTSC=0x%08x CCS=%u PED=%u PR=%u PP=%u speed=%u CSC=%u PEC=%u WRC=%u OCC=%u PRC=%u PLC=%u CEC=%u CAS=%u",
          controller_number,port_number,when,portsc,ccs,ped,pr,pp,speed,csc,pec,wrc,occ,prc,plc,cec,cas);
}

static bool reset_port(uint8_t port_number, uint8_t *speed){
    volatile uint8_t *port_base=(volatile uint8_t*)(void*)operational+0x400;
    volatile uint32_t *port=(volatile uint32_t*)(void*)(port_base
                                                +(port_number-1)*0x10);
    uint32_t status=port[0];
    klogf(KLOG_DEBUG,"xhci%u: port%u: reset_port entry PORTSC=0x%08x",controller_number,port_number,status);
    xhci_log_port(port_number,status,"probe-entry");
    if(!(status&XHCI_PORT_CONNECTED)){
        klogf(KLOG_DEBUG,"xhci%u: port%u no CCS, skip (PORTSC=0x%08x)",controller_number,port_number,status);
        return false;
    }
    probe_stats.connected_ports++;
    probe_stats.last_port=port_number;
    probe_stats.last_portsc=status;
    klogf(KLOG_INFO,"xhci%u: port%u CCS=1 detected, PED=%u PP=%u speed=%u CSC=%u",
          controller_number,port_number,(status>>1)&1,(status>>9)&1,(status>>10)&0xF,(status>>17)&1);
    if(status&XHCI_PORT_ENABLED){
        *speed=(uint8_t)((status>>XHCI_PORT_SPEED_SHIFT)&0x0F);
        klogf(KLOG_INFO,"xhci%u: port%u already enabled speed=%u, clearing CSC/PEC",controller_number,port_number,*speed);
        port[0]=(status&~(XHCI_PORT_CHANGE_BITS|XHCI_PORT_ENABLED
                          |XHCI_PORT_RESET))|(status&XHCI_PORT_CHANGE_BITS);
        uint32_t after=port[0];
        xhci_log_port(port_number,after,"after-clear-enabled");
        if(!*speed){
            probe_stats.last_error=XHCI_PROBE_PORT_RESET;
            klogf(KLOG_WARN,"xhci%u: port%u enabled but speed=0 -> reset failed",controller_number,port_number);
        }
        return *speed!=0;
    }
    if(!(status&XHCI_PORT_POWER)){
        klogf(KLOG_WARN,"xhci%u: port%u PP=0, powering on",controller_number,port_number);
        port[0]=(status&~(XHCI_PORT_CHANGE_BITS|XHCI_PORT_ENABLED
                          |XHCI_PORT_RESET))|XHCI_PORT_POWER;
        delay_ms(50);
        status=port[0];
        xhci_log_port(port_number,status,"after-PP-set");
        if(!(status&XHCI_PORT_CONNECTED)){
            probe_stats.last_error=XHCI_PROBE_PORT_RESET;
            probe_stats.last_portsc=status;
            klogf(KLOG_WARN,"xhci%u: port%u lost CCS after powering PP (PORTSC=0x%08x)",controller_number,port_number,status);
            return false;
        }
        if(!(status&XHCI_PORT_POWER)){
            klogf(KLOG_WARN,"xhci%u: port%u PP still 0 after SET (PORTSC=0x%08x)",controller_number,port_number,status);
        }
        delay_ms(20);
    }
    uint32_t writable=status&~(XHCI_PORT_CHANGE_BITS|XHCI_PORT_ENABLED
                               |XHCI_PORT_RESET);
    klogf(KLOG_INFO,"xhci%u: port%u issuing PR (Port Reset) write 0x%08x",controller_number,port_number,writable|XHCI_PORT_POWER|XHCI_PORT_RESET);
    port[0]=writable|XHCI_PORT_POWER|XHCI_PORT_RESET;
    if(!wait_register(&port[0],XHCI_PORT_RESET,false)){
        probe_stats.last_error=XHCI_PROBE_PORT_RESET;
        probe_stats.last_portsc=port[0];
        xhci_log_port(port_number,port[0],"PR-timeout");
        klogf(KLOG_ERROR,"xhci%u: port%u PR timeout (PORTSC=0x%08x)",controller_number,port_number,port[0]);
        return false;
    }
    delay_ms(50);
    status=port[0];
    xhci_log_port(port_number,status,"after-PR");
    probe_stats.last_portsc=status;
    if(!(status&XHCI_PORT_CONNECTED) || !(status&XHCI_PORT_ENABLED)){
        probe_stats.last_error=XHCI_PROBE_PORT_RESET;
        klogf(KLOG_ERROR,"xhci%u: port%u after reset no CCS/PED (CCS=%u PED=%u PORTSC=0x%08x)",
              controller_number,port_number,status&1,(status>>1)&1,status);
        return false;
    }
    *speed=(uint8_t)((status>>XHCI_PORT_SPEED_SHIFT)&0x0F);
    klogf(KLOG_INFO,"xhci%u: port%u reset OK speed=%u PED=%u PORTSC=0x%08x",controller_number,port_number,*speed,(status>>1)&1,status);
    port[0]=(status&~(XHCI_PORT_CHANGE_BITS|XHCI_PORT_ENABLED
                      |XHCI_PORT_RESET))|(status&XHCI_PORT_CHANGE_BITS);
    uint32_t cleared=port[0];
    xhci_log_port(port_number,cleared,"after-clear-PRC");
    return *speed!=0;
}

static bool update_control_packet_size(uint8_t index, uint16_t packet_size){
    uint8_t slot=devices[index].slot_id;
    uint8_t *input=input_contexts[index];
    uint8_t *output=output_contexts[index];
    memset(input,0,4096);
    context_at(input,0)[1]=(1U<<1);
    memcpy(context_at(input,2),context_at(output,1),context_size);
    uint32_t *ep0=context_at(input,2);
    ep0[1]=(ep0[1]&0x0000FFFFU)|((uint32_t)packet_size<<16);
    return submit_command(physical_address(input),XHCI_TRB_EVALUATE_CTX,slot,0);
}

static bool address_port(uint8_t device_index, uint8_t port, uint8_t speed){
    uint8_t slot;
    if(!submit_command(0,XHCI_TRB_ENABLE_SLOT,0,&slot) || !slot){
        probe_stats.last_error=XHCI_PROBE_ENABLE_SLOT;
        return false;
    }
    devices[device_index].slot_id=slot;
    memset(input_contexts[device_index],0,4096);
    memset(output_contexts[device_index],0,4096);
    initialize_ring(&control_rings[device_index],control_trbs[device_index]);
    device_context_base[slot]=physical_address(output_contexts[device_index]);

    uint8_t *input=input_contexts[device_index];
    context_at(input,0)[1]=(1U<<0)|(1U<<1);
    uint32_t *slot_context=context_at(input,1);
    slot_context[0]=((uint32_t)speed<<20)|(1U<<27);
    slot_context[1]=(uint32_t)port<<16;
    uint32_t *ep0=context_at(input,2);
    ep0[1]=(3U<<1)|(4U<<3)|((uint32_t)initial_packet_size(speed)<<16);
    uint64_t ring_address=physical_address(control_trbs[device_index])|1;
    ep0[2]=(uint32_t)ring_address;
    ep0[3]=(uint32_t)(ring_address>>32);
    ep0[4]=8;
    __sync_synchronize();
    if(!submit_command(physical_address(input),XHCI_TRB_ADDRESS_DEVICE,slot,0)){
        probe_stats.last_error=XHCI_PROBE_ADDRESS_DEVICE;
        return false;
    }
    probe_stats.addressed_devices++;
    return true;
}

static bool get_descriptor(uint8_t index, uint8_t type, uint8_t descriptor_index,
                           uint16_t length){
    memset(descriptor_buffer,0,sizeof(descriptor_buffer));
    return control_transfer(index,0x80,6,
                            (uint16_t)((uint16_t)type<<8)|descriptor_index,
                            0,descriptor_buffer,length,true);
}

static bool find_mass_storage_interface(uint16_t total_length,
                                        uint8_t *configuration,
                                        uint8_t *interface_number,
                                        uint8_t *bulk_in, uint16_t *bulk_in_packet,
                                        uint8_t *bulk_out, uint16_t *bulk_out_packet){
    bool mass_interface=false;
    for(uint16_t offset=0;offset+2<=total_length;){
        uint8_t length=descriptor_buffer[offset];
        uint8_t type=descriptor_buffer[offset+1];
        if(length<2 || offset+length>total_length) return false;
        if(type==USB_DESCRIPTOR_CONFIGURATION && length>=9){
            *configuration=descriptor_buffer[offset+5];
        } else if(type==USB_DESCRIPTOR_INTERFACE && length>=9){
            mass_interface=descriptor_buffer[offset+5]==USB_CLASS_MASS_STORAGE
                && descriptor_buffer[offset+7]==USB_PROTOCOL_BULK_ONLY;
            if(mass_interface) *interface_number=descriptor_buffer[offset+2];
        } else if(type==USB_DESCRIPTOR_ENDPOINT && length>=7 && mass_interface
                  && (descriptor_buffer[offset+3]&3)==2){
            uint8_t address=descriptor_buffer[offset+2];
            uint16_t packet=(uint16_t)descriptor_buffer[offset+4]
                |((uint16_t)descriptor_buffer[offset+5]<<8);
            if(address&0x80){ *bulk_in=address; *bulk_in_packet=packet; }
            else { *bulk_out=address; *bulk_out_packet=packet; }
        }
        offset+=length;
    }
    return *configuration && *bulk_in && *bulk_out;
}

static bool find_boot_mouse_interface(uint16_t total_length,
                                      uint8_t *configuration,
                                      uint8_t *interface_number,
                                      uint8_t *interrupt_in,
                                      uint16_t *packet_size,
                                      uint8_t *interval){
    bool mouse_interface=false;
    for(uint16_t offset=0;offset+2<=total_length;){
        uint8_t length=descriptor_buffer[offset];
        uint8_t type=descriptor_buffer[offset+1];
        if(length<2 || offset+length>total_length) return false;
        if(type==USB_DESCRIPTOR_CONFIGURATION && length>=9){
            *configuration=descriptor_buffer[offset+5];
        } else if(type==USB_DESCRIPTOR_INTERFACE && length>=9){
            if(descriptor_buffer[offset+5]==USB_CLASS_HID){
                probe_stats.hid_interfaces++;
                klogf(KLOG_DEBUG,"xhci%u: HID interface=%u subclass=%u protocol=%u",
                      controller_number,descriptor_buffer[offset+2],
                      descriptor_buffer[offset+6],descriptor_buffer[offset+7]);
            }
            if(descriptor_buffer[offset+5]==USB_CLASS_HUB){
                probe_stats.hubs++;
            }
            mouse_interface=descriptor_buffer[offset+5]==USB_CLASS_HID
                && descriptor_buffer[offset+6]==USB_HID_SUBCLASS_BOOT
                && (descriptor_buffer[offset+7]==USB_HID_PROTOCOL_MOUSE
                    || descriptor_buffer[offset+7]==0);
            if(mouse_interface) *interface_number=descriptor_buffer[offset+2];
        } else if(type==USB_DESCRIPTOR_ENDPOINT && length>=7
                  && mouse_interface && (descriptor_buffer[offset+3]&3)==3
                  && (descriptor_buffer[offset+2]&0x80)){
            *interrupt_in=descriptor_buffer[offset+2];
            *packet_size=(uint16_t)descriptor_buffer[offset+4]
                |((uint16_t)descriptor_buffer[offset+5]<<8);
            *interval=descriptor_buffer[offset+6];
        }
        offset+=length;
    }
    return *configuration && *interrupt_in && *packet_size;
}

static void configure_endpoint_context(uint32_t *endpoint, uint8_t type,
                                       uint16_t max_packet,
                                       struct xhci_ring_state *ring){
    endpoint[1]=(3U<<1)|((uint32_t)type<<3)|((uint32_t)max_packet<<16);
    uint64_t dequeue=physical_address(ring->trbs)|1;
    endpoint[2]=(uint32_t)dequeue;
    endpoint[3]=(uint32_t)(dequeue>>32);
    endpoint[4]=max_packet;
}

static bool configure_mass_storage(uint8_t index, uint8_t configuration,
                                   uint8_t bulk_in, uint16_t bulk_in_packet,
                                   uint8_t bulk_out, uint16_t bulk_out_packet){
    uint8_t in_dci=(uint8_t)((bulk_in&0x0F)*2+1);
    uint8_t out_dci=(uint8_t)((bulk_out&0x0F)*2);
    uint8_t highest=in_dci>out_dci ? in_dci : out_dci;
    if(!in_dci || !out_dci || highest>=32) return false;

    initialize_ring(&bulk_in_rings[index],bulk_in_trbs[index]);
    initialize_ring(&bulk_out_rings[index],bulk_out_trbs[index]);
    uint8_t *input=input_contexts[index];
    uint8_t *output=output_contexts[index];
    memset(input,0,4096);
    context_at(input,0)[1]=(1U<<0)|(1U<<in_dci)|(1U<<out_dci);
    memcpy(context_at(input,1),context_at(output,0),context_size);
    uint32_t *slot=context_at(input,1);
    slot[0]=(slot[0]&~(0x1FU<<27))|((uint32_t)highest<<27);
    configure_endpoint_context(context_at(input,(uint8_t)(in_dci+1)),6,
                               bulk_in_packet,&bulk_in_rings[index]);
    configure_endpoint_context(context_at(input,(uint8_t)(out_dci+1)),2,
                               bulk_out_packet,&bulk_out_rings[index]);
    if(!submit_command(physical_address(input),XHCI_TRB_CONFIGURE_EP,
                       devices[index].slot_id,0)){
        return false;
    }
    if(!control_transfer(index,0,9,configuration,0,0,0,false)) return false;
    devices[index].bulk_in_dci=in_dci;
    devices[index].bulk_out_dci=out_dci;
    devices[index].bulk_in_packet=bulk_in_packet;
    devices[index].bulk_out_packet=bulk_out_packet;
    return true;
}

static bool configure_boot_mouse(uint8_t index, uint8_t configuration,
                                 uint8_t interface_number,
                                 uint8_t interrupt_in, uint16_t packet_size,
                                 uint8_t interval, uint8_t speed){
    uint8_t dci=(uint8_t)((interrupt_in&0x0F)*2+1);
    packet_size&=0x7FF;
    if(dci<3 || dci>=32 || packet_size==0) return false;

    initialize_ring(&bulk_in_rings[index],bulk_in_trbs[index]);
    uint8_t *input=input_contexts[index];
    uint8_t *output=output_contexts[index];
    memset(input,0,4096);
    context_at(input,0)[1]=(1U<<0)|(1U<<dci);
    memcpy(context_at(input,1),context_at(output,0),context_size);
    uint32_t *slot=context_at(input,1);
    slot[0]=(slot[0]&~(0x1FU<<27))|((uint32_t)dci<<27);
    uint32_t *endpoint=context_at(input,(uint8_t)(dci+1));
    configure_endpoint_context(endpoint,7,packet_size,&bulk_in_rings[index]);
    uint8_t xhci_interval;
    if(speed<=2){
        xhci_interval=fs_ls_interrupt_interval(interval);
        klogf(KLOG_DEBUG,"xhci%u: FS/LS mouse interval bInterval=%u -> xHCI interval=%u",controller_number,interval,xhci_interval);
    } else {
        if(interval<1) interval=1;
        if(interval>16) interval=16;
        xhci_interval=(uint8_t)(interval-1);
        klogf(KLOG_DEBUG,"xhci%u: HS/SS mouse interval bInterval=%u -> xHCI interval=%u",controller_number,interval,xhci_interval);
    }
    endpoint[0]=(uint32_t)xhci_interval<<16;
    uint16_t report_length=packet_size<8 ? packet_size : 8;
    endpoint[4]=(uint32_t)packet_size|((uint32_t)report_length<<16);
    if(!submit_command(physical_address(input),XHCI_TRB_CONFIGURE_EP,
                       devices[index].slot_id,0)) return false;
    if(!control_transfer(index,0,9,configuration,0,0,0,false)) return false;
    if(!control_transfer(index,0x21,11,0,interface_number,0,0,false)){
        klogf(KLOG_WARN,"xhci%u: HID mouse rejected SET_PROTOCOL boot",controller_number);
    }
    (void)control_transfer(index,0x21,10,0,interface_number,0,0,false);
    devices[index].kind=XHCI_DEVICE_MOUSE;
    devices[index].interrupt_in_dci=dci;
    devices[index].interrupt_packet=packet_size;
    devices[index].interrupt_errors=0;
    devices[index].interrupt_pending=false;
    return true;
}

static bool bulk_only_command(uint8_t index, const uint8_t *command,
                              uint8_t command_length, void *data,
                              uint32_t data_length, bool data_in){
    struct xhci_device *device=&devices[index];
    memset(&command_block,0,sizeof(command_block));
    uint32_t tag=bot_tag++;
    command_block.signature=0x43425355;
    command_block.tag=tag;
    command_block.transfer_length=data_length;
    command_block.flags=data_in ? 0x80 : 0;
    command_block.command_length=command_length;
    memcpy(command_block.command,command,command_length);
    if(!transfer(index,device->bulk_out_dci,&bulk_out_rings[index],
                 &command_block,sizeof(command_block))){
        return false;
    }
    if(data_length){
        struct xhci_ring_state *ring=data_in
            ? &bulk_in_rings[index] : &bulk_out_rings[index];
        uint8_t endpoint=data_in ? device->bulk_in_dci : device->bulk_out_dci;
        if(!transfer(index,endpoint,ring,data,data_length)) return false;
    }
    memset(&command_status,0,sizeof(command_status));
    if(!transfer(index,device->bulk_in_dci,&bulk_in_rings[index],
                 &command_status,sizeof(command_status))){
        return false;
    }
    return command_status.signature==0x53425355
        && command_status.tag==tag && command_status.status==0;
}

static void build_device_name(char output[STORAGE_DEVICE_NAME_CAPACITY],
                              uint32_t name_index){
    memset(output,0,STORAGE_DEVICE_NAME_CAPACITY);
    output[0]='/'; output[1]='d'; output[2]='e'; output[3]='v';
    output[4]='/'; output[5]='s'; output[6]='d';
    output[7]=(char)('a'+name_index);
}

static void build_usb_serial(char output[STORAGE_SERIAL_CAPACITY], uint16_t vendor,
                             uint16_t product, uint8_t port){
    const char *hex="0123456789ABCDEF";
    memcpy(output,"USB-",4);
    for(uint8_t index=0;index<4;index++) output[4+index]=hex[(vendor>>(12-index*4))&0xF];
    output[8]='-';
    for(uint8_t index=0;index<4;index++) output[9+index]=hex[(product>>(12-index*4))&0xF];
    output[13]='-'; output[14]='P';
    output[15]=hex[(port>>4)&0xF]; output[16]=hex[port&0xF]; output[17]='\0';
}

static void copy_inquiry_model(char output[STORAGE_MODEL_CAPACITY],
                               const uint8_t *inquiry){
    uint8_t length=0;
    for(uint8_t index=8;index<36 && length+1<STORAGE_MODEL_CAPACITY;index++){
        char character=(char)inquiry[index];
        if(character<' ' || character>'~') character=' ';
        output[length++]=character;
    }
    while(length && output[length-1]==' ') length--;
    output[length]='\0';
    if(!length) memcpy(output,"USB Mass Storage",17);
}

static bool identify_mass_storage(uint8_t index, uint32_t name_index,
                                  uint8_t port, uint16_t vendor, uint16_t product){
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
    memset(transfer_buffer,0,sizeof(transfer_buffer));
    if(!bulk_only_command(index,command,6,transfer_buffer,36,true)) return false;

    struct storage_device_info *info=&devices[index].info;
    memset(info,0,sizeof(*info));
    build_device_name(info->name,name_index);
    copy_inquiry_model(info->model,transfer_buffer);
    build_usb_serial(info->serial,vendor,product,port);
    memset(command,0,sizeof(command));
    command[0]=SCSI_READ_CAPACITY10;
    if(!bulk_only_command(index,command,10,transfer_buffer,8,true)) return false;
    uint32_t last_lba=read_be32(transfer_buffer);
    uint32_t block_size=read_be32(&transfer_buffer[4]);
    if(last_lba==0xFFFFFFFF || block_size!=512) return false;
    info->sector_count=(uint64_t)last_lba+1;
    info->sector_size=block_size;
    info->transport=STORAGE_TRANSPORT_USB_MSC;
    info->controller=controller_number;
    info->port=port;
    devices[index].sync_cache_supported=true;
    memset(command,0,sizeof(command));
    command[0]=SCSI_MODE_SENSE6;
    command[2]=0x3F;
    command[4]=4;
    info->writable=1;
    if(bulk_only_command(index,command,6,transfer_buffer,4,true)){
        info->writable=(transfer_buffer[2]&0x80)==0;
    }
    info->operational=1;
    return info->sector_count!=0;
}

static bool enumerate_port(uint8_t port, uint8_t speed, uint32_t name_index){
    uint8_t index=slot_count;
    memset(&devices[index],0,sizeof(devices[index]));
    probe_stats.last_stage=3;
    klogf(KLOG_INFO,"xhci%u: enumerate port%u -> slot alloc index=%u speed=%u",controller_number,port,index,speed);
    if(!address_port(index,port,speed)){
        klogf(KLOG_ERROR,"xhci%u: address_port failed port=%u speed=%u error=%u completion=%u",controller_number,port,speed,probe_stats.last_error,probe_stats.last_completion_code);
        if(probe_stats.last_error==XHCI_PROBE_OK
           || probe_stats.last_error==XHCI_PROBE_EVENT_TIMEOUT
           || probe_stats.last_error==XHCI_PROBE_COMPLETION){
            probe_stats.last_error=XHCI_PROBE_ADDRESS_DEVICE;
        }
        return false;
    }
    klogf(KLOG_INFO,"xhci%u: slot %u addressed for port %u speed %u",controller_number,devices[index].slot_id,port,speed);
    probe_stats.last_stage=4;
    if(!get_descriptor(index,USB_DESCRIPTOR_DEVICE,0,18)){
        probe_stats.last_error=XHCI_PROBE_DEVICE_DESCRIPTOR;
        klogf(KLOG_ERROR,"xhci%u: get DEVICE descriptor failed port %u slot %u",controller_number,port,devices[index].slot_id);
        return false;
    }
    klogf(KLOG_INFO,"xhci%u: DEVICE desc port%u: vid=0x%04x pid=0x%04x bcdDevice=0x%04x class=%u/%u proto=%u packet=%u",
          controller_number,port,(unsigned)descriptor_buffer[8]|((unsigned)descriptor_buffer[9]<<8),(unsigned)descriptor_buffer[10]|((unsigned)descriptor_buffer[11]<<8),
          (unsigned)descriptor_buffer[12]|((unsigned)descriptor_buffer[13]<<8),descriptor_buffer[4],descriptor_buffer[5],descriptor_buffer[6],descriptor_buffer[7]);
    uint16_t vendor=(uint16_t)descriptor_buffer[8]
        |((uint16_t)descriptor_buffer[9]<<8);
    uint16_t product=(uint16_t)descriptor_buffer[10]
        |((uint16_t)descriptor_buffer[11]<<8);
    uint16_t packet=descriptor_buffer[7];
    if(speed>=4) packet=(uint16_t)(1U<<packet);
    klogf(KLOG_INFO,"xhci%u: ep0 packet raw=%u adjusted=%u initial=%u speed=%u",controller_number,descriptor_buffer[7],packet,initial_packet_size(speed),speed);
    if(packet && packet!=initial_packet_size(speed)
       && !update_control_packet_size(index,packet)){
        klogf(KLOG_ERROR,"xhci%u: update_control_packet_size failed to %u",controller_number,packet);
        return false;
    }
    if(!get_descriptor(index,USB_DESCRIPTOR_CONFIGURATION,0,9)){
        probe_stats.last_error=XHCI_PROBE_CONFIG_DESCRIPTOR;
        klogf(KLOG_ERROR,"xhci%u: get CONFIG header failed port %u",controller_number,port);
        return false;
    }
    uint16_t total=(uint16_t)descriptor_buffer[2]
        |((uint16_t)descriptor_buffer[3]<<8);
    klogf(KLOG_INFO,"xhci%u: CONFIG header total=%u buf[2]=%u buf[3]=%u",controller_number,total,descriptor_buffer[2],descriptor_buffer[3]);
    if(total<9 || total>sizeof(descriptor_buffer)
       || !get_descriptor(index,USB_DESCRIPTOR_CONFIGURATION,0,total)){
        probe_stats.last_error=XHCI_PROBE_CONFIG_DESCRIPTOR;
        klogf(KLOG_ERROR,"xhci%u: CONFIG full %u bytes failed (limit %u)",controller_number,total,(unsigned)sizeof(descriptor_buffer));
        return false;
    }
    // dump first 64 bytes of config descriptor for diagnosis
    klogf(KLOG_DEBUG,"xhci%u: CONFIG dump port%u total=%u",controller_number,port,total);
    for(uint16_t off=0; off<total && off<64; off+=16){
        klogf(KLOG_DEBUG,"xhci%u: cfg+%02x: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
              controller_number,off,
              descriptor_buffer[off], off+1<total?descriptor_buffer[off+1]:0, off+2<total?descriptor_buffer[off+2]:0, off+3<total?descriptor_buffer[off+3]:0,
              off+4<total?descriptor_buffer[off+4]:0, off+5<total?descriptor_buffer[off+5]:0, off+6<total?descriptor_buffer[off+6]:0, off+7<total?descriptor_buffer[off+7]:0,
              off+8<total?descriptor_buffer[off+8]:0, off+9<total?descriptor_buffer[off+9]:0, off+10<total?descriptor_buffer[off+10]:0, off+11<total?descriptor_buffer[off+11]:0,
              off+12<total?descriptor_buffer[off+12]:0, off+13<total?descriptor_buffer[off+13]:0, off+14<total?descriptor_buffer[off+14]:0, off+15<total?descriptor_buffer[off+15]:0);
    }
    uint8_t configuration=0,interface_number=0,bulk_in=0,bulk_out=0;
    uint16_t bulk_in_packet=0,bulk_out_packet=0;
    if(find_mass_storage_interface(total,&configuration,&interface_number,
                                   &bulk_in,&bulk_in_packet,
                                   &bulk_out,&bulk_out_packet)){
        klogf(KLOG_INFO,"xhci%u: BOT interface cfg=%u if=%u bulkIn=0x%02x pkt=%u bulkOut=0x%02x pkt=%u",controller_number,configuration,interface_number,bulk_in,bulk_in_packet,bulk_out,bulk_out_packet);
        probe_stats.last_stage=5;
        if(!configure_mass_storage(index,configuration,bulk_in,bulk_in_packet,
                                   bulk_out,bulk_out_packet)){
            probe_stats.last_error=XHCI_PROBE_CONFIGURE_ENDPOINT;
            return false;
        }
        devices[index].kind=XHCI_DEVICE_STORAGE;
        if(!identify_mass_storage(index,name_index,port,vendor,product)){
            probe_stats.last_error=XHCI_PROBE_SCSI;
            return false;
        }
        slot_count++;
        device_count++;
        probe_stats.mass_storage_devices++;
        probe_stats.last_stage=7;
        probe_stats.last_error=XHCI_PROBE_OK;
        klogf(KLOG_OK,"xhci%u: USB MSC ready %s port%u",controller_number,
              devices[index].info.name,port);
        return true;
    }

    uint8_t interrupt_in=0,interval=0;
    uint16_t interrupt_packet=0;
    configuration=0;
    interface_number=0;
    if(find_boot_mouse_interface(total,&configuration,&interface_number,
                                 &interrupt_in,&interrupt_packet,&interval)){
        if(!configure_boot_mouse(index,configuration,interface_number,
                                 interrupt_in,interrupt_packet,interval,speed)){
            probe_stats.last_error=XHCI_PROBE_CONFIGURE_ENDPOINT;
            return false;
        }
        slot_count++;
        probe_stats.hid_mice++;
        probe_stats.last_stage=7;
        probe_stats.last_error=XHCI_PROBE_OK;
        usb_mouse_attach(vendor,product,port);
        return true;
    }

    slot_count++;
    probe_stats.last_error=XHCI_PROBE_OK;
    klogf(KLOG_INFO,"xhci%u: port%u device class is not MSC or HID boot mouse",
          controller_number,port);
    return true;
}

static bool take_ownership(volatile uint32_t *capability, uint32_t hccparams1){
    uint16_t offset=(uint16_t)(hccparams1>>16)*4;
    klogf(KLOG_INFO,"xhci%u: HCCPARAMS1=0x%08x xECP offset=0x%x",controller_number,hccparams1,offset);
    if(!offset){
        klogf(KLOG_INFO,"xhci%u: no xECP, no BIOS handoff needed",controller_number);
        return true;
    }
    for(uint8_t visited=0;offset && visited<64;visited++){
        volatile uint32_t *extended=(volatile uint32_t*)((volatile uint8_t*)capability+offset);
        uint32_t header=extended[0];
        uint8_t cap_id=(uint8_t)(header&0xFF);
        uint8_t next=(uint8_t)((header>>8)&0xFF);
        uint32_t cap_header_raw=header;
        klogf(KLOG_INFO,"xhci%u: xECP cap %u @0x%x id=0x%02x next=0x%02x header=0x%08x",controller_number,visited,offset,cap_id,next,cap_header_raw);
        if(cap_id==1){
            uint32_t before=extended[0];
            uint32_t before1=extended[1];
            klogf(KLOG_INFO,"xhci%u: USB Legacy BIOS handoff cap found LEGCTLSTS=0x%08x CTRL=0x%08x BIOS owned=%u OS owned=%u",
                  controller_number,before,before1,(before>>16)&1,(before>>24)&1);
            extended[0]=header|(1U<<24);
            klogf(KLOG_INFO,"xhci%u: set OS ownership (LEGSUP OS-owned)",controller_number);
            uint64_t deadline=rdtsc()+(uint64_t)tsc_mhz()*1000ULL*1000ULL;
            while((extended[0]&(1U<<16))
                  && (int64_t)(rdtsc()-deadline)<0){
                __asm__ volatile("pause");
            }
            uint32_t after=extended[0];
            if(after&(1U<<16)){
                probe_stats.last_error=XHCI_PROBE_BIOS_HANDOFF;
                klogf(KLOG_WARN,"xhci%u: BIOS ownership timeout BIOS still owns after 1s LEGSUP=0x%08x; forcing OS handoff",controller_number,after);
            } else {
                klogf(KLOG_OK,"xhci%u: BIOS handoff OK LEGSUP=0x%08x after",controller_number,after);
            }
            // Disable legacy SMI sources even when broken firmware keeps the
            // BIOS-owned semaphore asserted, matching the non-fatal Linux path.
            extended[1]=0;
            uint32_t after1=extended[1];
            klogf(KLOG_INFO,"xhci%u: LEGCTLSTS cleared 0x%08x -> 0x%08x",controller_number,before1,after1);
            return true;
        }
        if(!next) break;
        offset=(uint16_t)(offset+next*4);
    }
    klogf(KLOG_INFO,"xhci%u: no USB Legacy cap in xECP chain, continuing",controller_number);
    return true;
}

static bool initialize_controller(const struct storage_controller_info *controller,
                                  uint32_t linux_name_base){
    if(!controller->register_base){
        klogf(KLOG_ERROR,"xhci%u: PCI BAR is zero (pci %u:%u.%u)",controller_number,controller->bus,controller->slot,controller->function);
        probe_stats.last_error=XHCI_PROBE_MMIO;
        return false;
    }
    uint32_t pci_command=pci_read_config32(controller->bus,controller->slot,
                                            controller->function,0x04);
    klogf(KLOG_INFO,"xhci%u: PCI %u:%u.%u BAR=0x%llx CMD=0x%04x -> enable MEM/BUSMASTER",
          controller_number,controller->bus,controller->slot,controller->function,controller->register_base,pci_command);
    pci_write_config32(controller->bus,controller->slot,controller->function,
                       0x04,pci_command|0x06);
    uint32_t pci_command_after=pci_read_config32(controller->bus,controller->slot,controller->function,0x04);
    klogf(KLOG_INFO,"xhci%u: PCI CMD after=0x%04x",controller_number,pci_command_after);
    mmio_base=(volatile uint8_t*)mmio_map(controller->register_base,
                                          XHCI_MMIO_MAP_SIZE);
    if(!mmio_base){
        probe_stats.last_error=XHCI_PROBE_MMIO;
        klogf(KLOG_ERROR,"xhci%u: cannot map BAR 0x%llx size 0x%x",controller_number,
              controller->register_base,XHCI_MMIO_MAP_SIZE);
        return false;
    }
    klogf(KLOG_INFO,"xhci%u: BAR phys=0x%llx mapped=%p size=0x%x",controller_number,
          controller->register_base,(void*)mmio_base,XHCI_MMIO_MAP_SIZE);
    volatile uint32_t *capability=(volatile uint32_t*)(void*)mmio_base;
    uint8_t capability_length=mmio_base[0];
    uint16_t hc_version= (uint16_t)mmio_base[2]|((uint16_t)mmio_base[3]<<8);
    uint32_t hcsparams1=capability[1];
    uint32_t hcsparams2=capability[2];
    uint32_t hcsparams3=capability[3];
    uint32_t hccparams1=capability[4];
    uint32_t dboff=capability[5];
    uint32_t rtsoff=capability[6];
    klogf(KLOG_INFO,"xhci%u: CAPLENGTH=0x%02x HCIVERSION=0x%04x HCS1=0x%08x HCS2=0x%08x HCS3=0x%08x HCC1=0x%08x DBOFF=0x%08x RTSOFF=0x%08x",
          controller_number,capability_length,hc_version,hcsparams1,hcsparams2,hcsparams3,hccparams1,dboff,rtsoff);
    if(capability_length<0x20){
        probe_stats.last_error=XHCI_PROBE_CAPABILITY;
        klogf(KLOG_ERROR,"xhci%u: CAPLENGTH 0x%02x <0x20 invalid",controller_number,capability_length);
        return false;
    }
    if(!take_ownership(capability,hccparams1)) return false;
    context_size=(hccparams1&(1U<<2)) ? 64 : 32;
    uint8_t max_slots=(uint8_t)hcsparams1;
    uint8_t max_intrs=(uint8_t)(hcsparams1>>8);
    uint8_t max_ports=(uint8_t)(hcsparams1>>24);
    probe_stats.max_ports=max_ports;
    klogf(KLOG_INFO,"xhci%u: decoded maxSlots=%u maxIntrs=%u maxPorts=%u ctxSize=%u 64bit=%u CSZ=%u",
          controller_number,max_slots,max_intrs,max_ports,context_size,(hccparams1&1)!=0,(hccparams1>>2)&1);
    if(!max_slots || !max_ports){
        probe_stats.last_error=XHCI_PROBE_CAPABILITY;
        klogf(KLOG_ERROR,"xhci%u: max_slots or max_ports zero",controller_number);
        return false;
    }
    if(max_slots>32){
        klogf(KLOG_WARN,"xhci%u: max_slots %u exceeds 32, clamping",controller_number,max_slots);
        max_slots=32;
    }

    uint32_t doorbell_offset=capability[5]&~3U;
    uint32_t runtime_offset=capability[6]&~0x1FU;
    klogf(KLOG_INFO,"xhci%u: DBOFF=0x%x RTSOFF=0x%x CAP+0x40=0x%x",controller_number,doorbell_offset,runtime_offset,capability_length+0x40U);
    if((uint32_t)capability_length+0x40U>XHCI_MMIO_MAP_SIZE
       || doorbell_offset>XHCI_MMIO_MAP_SIZE-4U
       || runtime_offset>XHCI_MMIO_MAP_SIZE-0x40U){
        probe_stats.last_error=XHCI_PROBE_CAPABILITY;
        klogf(KLOG_ERROR,"xhci%u: offsets exceed MMIO mapping (caplen=%u db=0x%x rt=0x%x map=0x%x)",controller_number,capability_length,doorbell_offset,runtime_offset,XHCI_MMIO_MAP_SIZE);
        return false;
    }
    operational=(volatile uint32_t*)(void*)(mmio_base+capability_length);
    doorbells=(volatile uint32_t*)(void*)(mmio_base+doorbell_offset);
    runtime=(volatile uint32_t*)(void*)(mmio_base+runtime_offset);
    klogf(KLOG_INFO,"xhci%u: operational=%p doorbells=%p runtime=%p USBCMD=0x%08x USBSTS=0x%08x PAGESIZE=0x%08x",
          controller_number,(void*)operational,(void*)doorbells,(void*)runtime,operational[0],operational[1],operational[2]);
    operational[0]&=~XHCI_CMD_RUN;
    klogf(KLOG_INFO,"xhci%u: clearing RUN, waiting HALTED",controller_number);
    if(!wait_register(&operational[1],XHCI_STS_HALTED,true)){
        probe_stats.last_error=XHCI_PROBE_HALT_TIMEOUT;
        probe_stats.usb_status=operational[1];
        klogf(KLOG_ERROR,"xhci%u: halt timeout USBSTS=0x%08x USBCMD=0x%08x",controller_number,operational[1],operational[0]);
        return false;
    }
    klogf(KLOG_INFO,"xhci%u: HALTED, issuing RESET",controller_number);
    operational[0]|=XHCI_CMD_RESET;
    if(!wait_register(&operational[0],XHCI_CMD_RESET,false)){
        probe_stats.last_error=XHCI_PROBE_RESET_TIMEOUT;
        probe_stats.usb_status=operational[1];
        klogf(KLOG_ERROR,"xhci%u: reset timeout USBCMD=0x%08x USBSTS=0x%08x",controller_number,operational[0],operational[1]);
        return false;
    }
    klogf(KLOG_INFO,"xhci%u: RESET complete, waiting CNR clear",controller_number);
    if(!wait_register(&operational[1],XHCI_STS_NOT_READY,false)){
        probe_stats.last_error=XHCI_PROBE_NOT_READY_TIMEOUT;
        probe_stats.usb_status=operational[1];
        klogf(KLOG_ERROR,"xhci%u: CNR timeout USBSTS=0x%08x",controller_number,operational[1]);
        return false;
    }
    klogf(KLOG_INFO,"xhci%u: CNR cleared USBSTS=0x%08x PAGESIZE=0x%08x",controller_number,operational[1],operational[2]);
    if(!(operational[2]&1)){
        probe_stats.last_error=XHCI_PROBE_PAGE_SIZE;
        klogf(KLOG_ERROR,"xhci%u: 4K pages not supported PAGESIZE=0x%08x",controller_number,operational[2]);
        return false;
    }

    uint64_t dma_high=physical_address(device_context_base)
        |physical_address(command_trbs)|physical_address(event_trbs)
        |physical_address(input_contexts)|physical_address(output_contexts)
        |physical_address(control_trbs)|physical_address(bulk_in_trbs)
        |physical_address(bulk_out_trbs)|physical_address(descriptor_buffer)
        |physical_address(transfer_buffer)|physical_address(mouse_reports)
        |physical_address(&command_block)
        |physical_address(&command_status)|physical_address(&event_segment)
        |physical_address(scratchpad_pointers)|physical_address(scratchpad_pages);
    klogf(KLOG_INFO,"xhci%u: DMA addrs dcbaa=0x%llx cmd=0x%llx evt=0x%llx inCtx=0x%llx outCtx=0x%llx high32=0x%08x 64bitCap=%u",
          controller_number,physical_address(device_context_base),physical_address(command_trbs),physical_address(event_trbs),
          physical_address(input_contexts),physical_address(output_contexts),(uint32_t)(dma_high>>32),(hccparams1&1)!=0);
    if((dma_high>>32)!=0 && !(hccparams1&1)){
        probe_stats.last_error=XHCI_PROBE_DMA_ADDRESS;
        klogf(KLOG_ERROR,"xhci%u: DMA above 4G but 64-bit not supported (high=0x%08x HCC1=0x%08x)",controller_number,(uint32_t)(dma_high>>32),hccparams1);
        return false;
    }

    memset(device_context_base,0,sizeof(device_context_base));
    uint16_t scratchpads=(uint16_t)((hcsparams2>>27)&0x1F)
        |(uint16_t)((hcsparams2>>16)&0x3E0);
    probe_stats.scratchpad_count=scratchpads;
    klogf(KLOG_INFO,"xhci%u: scratchpads raw HCS2=0x%08x count=%u limit=%u",controller_number,hcsparams2,scratchpads,XHCI_SCRATCHPAD_LIMIT);
    // Also show alternate decoding for sanity: Linux uses ((HCS2>>27)&0x1F) | ((HCS2>>21)&0x3E0?) but we keep current formula and warn if suspicious
    uint16_t alt_scratch=(uint16_t)((hcsparams2>>27)&0x1F)|((uint16_t)((hcsparams2>>21)&0x1F)<<5);
    if(scratchpads!=alt_scratch) klogf(KLOG_WARN,"xhci%u: scratch decode alt=%u (HCS2>>21) differs, using %u",controller_number,alt_scratch,scratchpads);
    if(scratchpads>XHCI_SCRATCHPAD_LIMIT){
        probe_stats.last_error=XHCI_PROBE_SCRATCHPADS;
        klogf(KLOG_ERROR,"xhci%u: scratchpads %u > limit %u",controller_number,scratchpads,XHCI_SCRATCHPAD_LIMIT);
        return false;
    }
    if(scratchpads){
        for(uint16_t index=0;index<scratchpads;index++){
            scratchpad_pointers[index]=physical_address(scratchpad_pages[index]);
            if((index&0x7)==0) klogf(KLOG_DEBUG,"xhci%u: scratchpad[%u] phys=0x%llx",controller_number,index,scratchpad_pointers[index]);
        }
        device_context_base[0]=physical_address(scratchpad_pointers);
        klogf(KLOG_INFO,"xhci%u: DCBAA[0] scratch phys=0x%llx",controller_number,device_context_base[0]);
    } else {
        klogf(KLOG_INFO,"xhci%u: no scratchpads required",controller_number);
    }
    initialize_ring(&command_ring,command_trbs);
    uint64_t command_address=physical_address(command_trbs)|1;
    klogf(KLOG_INFO,"xhci%u: CRCR=0x%llx DCBAAP=0x%llx maxSlots=%u",controller_number,command_address,physical_address(device_context_base),max_slots);
    operational[6]=(uint32_t)command_address;
    operational[7]=(uint32_t)(command_address>>32);
    uint64_t dcbaa_address=physical_address(device_context_base);
    operational[12]=(uint32_t)dcbaa_address;
    operational[13]=(uint32_t)(dcbaa_address>>32);
    operational[14]=max_slots;

    memset(event_trbs,0,sizeof(event_trbs));
    event_index=0;
    event_cycle=1;
    uint64_t event_address=physical_address(event_trbs);
    event_segment.base_low=(uint32_t)event_address;
    event_segment.base_high=(uint32_t)(event_address>>32);
    event_segment.size=XHCI_EVENT_ENTRIES;
    event_segment.reserved=0;
    volatile uint32_t *interrupter=runtime+0x20/4;
    klogf(KLOG_INFO,"xhci%u: ERST phys=0x%llx ERSTBA=0x%llx ERDP=0x%llx IMAN before=0x%08x IMOD=0x%08x ERSTSZ before=0x%08x",
          controller_number,event_address,physical_address(&event_segment),event_address,interrupter[0],interrupter[1],interrupter[2]);
    interrupter[2]=1; // ERSTSZ = one segment (original correct)
    uint64_t erst_address=physical_address(&event_segment);
    interrupter[4]=(uint32_t)erst_address;
    interrupter[5]=(uint32_t)(erst_address>>32);
    interrupter[6]=(uint32_t)event_address;
    interrupter[7]=(uint32_t)(event_address>>32);
    __sync_synchronize();
    klogf(KLOG_INFO,"xhci%u: ERST configured ERSTSZ=%u ERSTBA=0x%08x%08x ERDP=0x%08x%08x",
          controller_number,interrupter[2],interrupter[5],interrupter[4],interrupter[7],interrupter[6]);
    klogf(KLOG_INFO,"xhci%u: starting controller USBCMD=0x%08x -> RUN",controller_number,operational[0]);
    operational[0]|=XHCI_CMD_RUN;
    if(!wait_register(&operational[1],XHCI_STS_HALTED,false)){
        probe_stats.last_error=XHCI_PROBE_RUN_TIMEOUT;
        probe_stats.usb_status=operational[1];
        klogf(KLOG_ERROR,"xhci%u: RUN timeout HALTED still 1 USBSTS=0x%08x USBCMD=0x%08x",controller_number,operational[1],operational[0]);
        return false;
    }
    probe_stats.usb_status=operational[1];
    klogf(KLOG_OK,"xhci%u: RUN OK USBSTS=0x%08x USBCMD=0x%08x",controller_number,operational[1],operational[0]);
    probe_stats.last_stage=2;
    klogf(KLOG_INFO,"xhci%u: controller RUN set, USBSTS=0x%08x, scanning %u ports",controller_number,probe_stats.usb_status,max_ports);
    // Dump raw PORTSC for all ports BEFORE reset attempts – самый полезный диагностический блок
    for(uint8_t p=1;p<=max_ports;p++){
        volatile uint8_t *port_base=(volatile uint8_t*)(void*)operational+0x400;
        volatile uint32_t *preg=(volatile uint32_t*)(void*)(port_base+(p-1)*0x10);
        uint32_t raw=preg[0];
        // extended xECP Protocol caps: we also decode speed id mapping later
        xhci_log_port(p,raw,"scan-pre");
    }
    // Also dump extended capability for protocol ports if present – помогает понять почему root port пустой
    {
        uint32_t hcc=hccparams1;
        uint16_t xecp=(uint16_t)(hcc>>16)*4;
        if(xecp){
            for(uint8_t v=0;xecp && v<16; v++){
                volatile uint32_t *ext=(volatile uint32_t*)((volatile uint8_t*)capability+xecp);
                uint32_t hdr=ext[0];
                uint8_t id=hdr&0xFF;
                uint8_t nxt=(hdr>>8)&0xFF;
                klogf(KLOG_INFO,"xhci%u: xECP@0x%x id=%u hdr=0x%08x val1=0x%08x val2=0x%08x",controller_number,xecp,id,hdr,ext[1],ext[2]);
                if(id==2){ // Supported Protocol Capability
                    uint8_t rev_min=ext[1]&0xFF;
                    uint8_t rev_maj=(ext[1]>>8)&0xFF;
                    uint8_t proto=ext[1]>>16; // simplified
                    uint8_t port_off=ext[2]&0xFF;
                    uint8_t port_cnt=(ext[2]>>8)&0xFF;
                    klogf(KLOG_INFO,"xhci%u: Supported Protocol proto=%u rev %u.%u ports off=%u cnt=%u",controller_number,proto,rev_maj,rev_min,port_off,port_cnt);
                }
                if(!nxt) break;
                xecp+=nxt*4;
            }
        }
    }

    for(uint8_t port=1;port<=max_ports && slot_count<XHCI_DEVICE_LIMIT;port++){
        uint8_t speed;
        klogf(KLOG_INFO,"xhci%u: probing port %u (%u/%u)",controller_number,port,port,max_ports);
        if(!reset_port(port,&speed)){
            klogf(KLOG_DEBUG,"xhci%u: port %u reset_port returned false, skip enumerate",controller_number,port);
            continue;
        }
        klogf(KLOG_INFO,"xhci%u: port %u reset OK speed=%u, calling enumerate",controller_number,port,speed);
        if(!enumerate_port(port,speed,linux_name_base+device_count)){
            probe_stats.failures++;
            klogf(KLOG_ERROR,"xhci%u: port %u enumerate failed error=%u (%u) last_stage=%u completion=%u portsc=0x%08x",
                  controller_number,port,probe_stats.last_error,probe_stats.last_error,probe_stats.last_stage,probe_stats.last_completion_code,probe_stats.last_portsc);
        } else {
            klogf(KLOG_OK,"xhci%u: port %u enumerate SUCCESS disks now %u",controller_number,port,probe_stats.mass_storage_devices);
        }
    }
    if(!probe_stats.connected_ports && !device_count){
        probe_stats.last_error=XHCI_PROBE_NO_CONNECTED_PORT;
        klogf(KLOG_WARN,"xhci%u: no connected ports after scan %u ports; checks:",controller_number,max_ports);
        klogf(KLOG_WARN,"xhci%u:  1) QEMU: устройства должны быть на xhci bus: -device qemu-xhci -device usb-storage,bus=xhci.0,drive=... иначе они попадут на UHCI/EHCI и будут невидимы здесь",controller_number);
        klogf(KLOG_WARN,"xhci%u:  2) BIOS handoff: проверь xECP логи выше (LEGSUP). QEMU должен отдать владельство OS.",controller_number);
        klogf(KLOG_WARN,"xhci%u:  3) MMIO BAR: raw BAR=0x%llx mapped=%p CAPLENGTH=%u HCSPARAMS1=0x%08x",controller_number,controller->register_base,(void*)mmio_base,capability[0]&0xFF,capability[1]);
        // Final dump of PORTSC after scan for пост-мортем
        for(uint8_t p=1;p<=max_ports;p++){
            volatile uint8_t *port_base=(volatile uint8_t*)(void*)operational+0x400;
            volatile uint32_t *preg=(volatile uint32_t*)(void*)(port_base+(p-1)*0x10);
            uint32_t raw=preg[0];
            xhci_log_port(p,raw,"scan-post");
        }
    } else {
        klogf(KLOG_OK,"xhci%u: scan done connected=%u addressed=%u disks=%u mice=%u failures=%u",controller_number,probe_stats.connected_ports,probe_stats.addressed_devices,probe_stats.mass_storage_devices,probe_stats.hid_mice,probe_stats.failures);
    }
    return true;
}

bool xhci_init(uint32_t linux_name_base){
    if(probe_complete){
        return device_count>0;
    }
    probe_complete=true;
    if(!mapping_ready){
        klog(KLOG_ERROR,"xhci: init mapping not ready");
        return false;
    }
    // Тяжёлую диагностику xhci пишем только в ring/dmesg, без мерцания GOP на реальном железе
    bool was_screen=klog_is_screen_enabled();
    klog_set_screen_enabled(false);
    struct storage_controller_info controllers[8];
    int32_t count=storage_controller_list(controllers,8);
    // подробный список PCI - только в dmesg
    for(int32_t i=0;i<count;i++){
        klogf(KLOG_INFO,"xhci: PCI controller[%d] type=%u name=%s bus %u:%u.%u BAR=0x%llx vend=%04x dev=%04x",
              i,controllers[i].type,controllers[i].name,controllers[i].bus,controllers[i].slot,controllers[i].function,
              controllers[i].register_base,controllers[i].vendor_id,controllers[i].device_id);
    }
    uint8_t xhci_index=0;
    for(int32_t index=0;index<count;index++){
        if(controllers[index].type!=STORAGE_CONTROLLER_XHCI) continue;
        probe_stats.controllers++;
        probe_stats.last_stage=1;
        controller_number=xhci_index++;
        if(!initialize_controller(&controllers[index],linux_name_base)){
            probe_stats.failures++;
            continue;
        }
        break;
    }
    klog_set_screen_enabled(was_screen);
    // Краткий итог - одна строка на экране загрузки, детали - в dmesg/usbscan
    if(probe_stats.controllers==0){
        klog(KLOG_INFO,"xhci: no controllers found");
    } else if(device_count==0 && probe_stats.connected_ports==0){
        klogf(KLOG_WARN,"xhci: controllers=%u ports=%u connected=0 disks=0 (детали в dmesg)",probe_stats.controllers,probe_stats.max_ports);
    } else {
        klogf(KLOG_INFO,"xhci: controllers=%u connected=%u disks=%u mice=%u",probe_stats.controllers,probe_stats.connected_ports,device_count,probe_stats.hid_mice);
    }
    known_port_bitmap=connected_port_bitmap();
    return device_count>0;
}

bool xhci_rescan(uint32_t linux_name_base){
    if(!mapping_ready){
        klog(KLOG_ERROR,"xhci: rescan mapping not ready");
        return false;
    }
    selected_device=XHCI_NO_DEVICE;
    usb_mouse_detach();
    slot_count=0;
    device_count=0;
    memset(devices,0,sizeof(devices));
    memset(device_context_base,0,sizeof(device_context_base));
    probe_stats.connected_ports=0;
    probe_stats.addressed_devices=0;
    probe_stats.mass_storage_devices=0;
    probe_stats.hid_mice=0;
    probe_stats.hid_interfaces=0;
    probe_stats.hubs=0;
    probe_stats.mouse_transfer_errors=0;
    probe_stats.failures=0;
    probe_stats.last_stage=0;
    probe_stats.last_error=XHCI_PROBE_OK;
    probe_stats.last_port=0;
    probe_stats.last_portsc=0;
    probe_stats.last_completion_code=0;
    probe_stats.max_ports=0;
    probe_stats.usb_status=0;
    probe_stats.scratchpad_count=0;

    // usbscan вызывается из userspace - там мерцания нет, можно писать подробно в ring,
    // но в терминал выводим только кратко, детали - через dmesg. Поэтому логируем в ring с выключенным экраном.
    bool was_screen=klog_is_screen_enabled();
    klog_set_screen_enabled(false);
    struct storage_controller_info controllers[8];
    int32_t count=storage_controller_list(controllers,8);
    if(count<0){
        klog_set_screen_enabled(was_screen);
        return false;
    }
    uint8_t xhci_index=0;
    for(int32_t index=0;index<count;index++){
        if(controllers[index].type!=STORAGE_CONTROLLER_XHCI) continue;
        probe_stats.last_stage=1;
        controller_number=xhci_index++;
        if(!initialize_controller(&controllers[index],linux_name_base)){
            probe_stats.failures++;
            continue;
        }
        break;
    }
    klog_set_screen_enabled(was_screen);
    known_port_bitmap=connected_port_bitmap();
    // краткий итог остаётся на экране userspace через syscall klog, но usbscan сам выводит детали
    klogf(KLOG_INFO,"xhci: rescan done disks=%u connected=%u addressed=%u error=%u stage=%u",device_count,probe_stats.connected_ports,probe_stats.addressed_devices,probe_stats.last_error,probe_stats.last_stage);
    return device_count>0;
}

static void queue_mouse_report(uint8_t index){
    struct xhci_device *device=&devices[index];
    struct xhci_ring_state *ring=&bulk_in_rings[index];
    uint32_t length=device->interrupt_packet;
    if(length>sizeof(mouse_reports[index])) length=sizeof(mouse_reports[index]);
    memset(mouse_reports[index],0,sizeof(mouse_reports[index]));
    struct xhci_trb *trb=ring_next(ring);
    uint64_t address=physical_address(mouse_reports[index]);
    trb->parameter_low=(uint32_t)address;
    trb->parameter_high=(uint32_t)(address>>32);
    trb->status=length;
    trb->control=(XHCI_TRB_NORMAL<<10)|(1U<<5)|ring->cycle;
    __sync_synchronize();
    device->interrupt_pending=true;
    doorbells[device->slot_id]=device->interrupt_in_dci;
}

static bool poll_async_event(void){
    volatile struct xhci_trb *event=&event_trbs[event_index];
    if((event->control&1)!=event_cycle) return false;
    __sync_synchronize();
    struct xhci_trb copy=*event;
    uint8_t type=(uint8_t)((copy.control>>10)&0x3F);
    event_index++;
    if(event_index==XHCI_EVENT_ENTRIES){ event_index=0; event_cycle^=1; }
    uint64_t dequeue=physical_address(&event_trbs[event_index]);
    volatile uint32_t *interrupter=runtime+0x20/4;
    interrupter[6]=(uint32_t)dequeue|8;
    interrupter[7]=(uint32_t)(dequeue>>32);
    if(type==XHCI_EVENT_TRANSFER) (void)dispatch_mouse_event(&copy);
    return true;
}

void xhci_poll_mouse(void){
    if(!operational || !doorbells || !runtime) return;
    for(uint16_t count=0;count<XHCI_EVENT_ENTRIES && poll_async_event();count++){}
    for(uint8_t index=0;index<slot_count;index++){
        if(devices[index].kind!=XHCI_DEVICE_MOUSE) continue;
        if(!devices[index].interrupt_pending) queue_mouse_report(index);
    }
}

uint32_t xhci_device_count(void){ return device_count; }

static uint8_t storage_device_index(uint32_t storage_index){
    uint32_t current=0;
    for(uint8_t index=0;index<slot_count;index++){
        if(devices[index].kind!=XHCI_DEVICE_STORAGE) continue;
        if(current==storage_index) return index;
        current++;
    }
    return XHCI_NO_DEVICE;
}

bool xhci_get_device_info(uint32_t index, struct storage_device_info *info){
    if(!info || index>=device_count) return false;
    uint8_t raw_index=storage_device_index(index);
    if(raw_index==XHCI_NO_DEVICE) return false;
    *info=devices[raw_index].info;
    return true;
}

bool xhci_select_device(uint32_t index){
    if(index>=device_count) return false;
    selected_device=storage_device_index(index);
    return selected_device!=XHCI_NO_DEVICE;
}

static bool scsi_sector_command(uint8_t operation, uint32_t lba, void *buffer,
                                bool data_in){
    if(selected_device>=slot_count
       || devices[selected_device].kind!=XHCI_DEVICE_STORAGE || !buffer) return false;
    struct xhci_device *device=&devices[selected_device];
    if(lba>=device->info.sector_count) return false;
    uint8_t command[16];
    memset(command,0,sizeof(command));
    command[0]=operation;
    write_be32(&command[2],lba);
    command[8]=1;
    return bulk_only_command(selected_device,command,10,buffer,512,data_in);
}

bool xhci_read_sector(uint32_t lba, void *buffer){
    return scsi_sector_command(SCSI_READ10,lba,buffer,true);
}

bool xhci_write_sector(uint32_t lba, const void *buffer){
    if(selected_device>=slot_count
       || devices[selected_device].kind!=XHCI_DEVICE_STORAGE
       || !devices[selected_device].info.writable){
        return false;
    }
    if(!scsi_sector_command(SCSI_WRITE10,lba,(void*)buffer,false)) return false;
    struct xhci_device *device=&devices[selected_device];
    if(!device->sync_cache_supported) return true;
    uint8_t command[16];
    memset(command,0,sizeof(command));
    command[0]=SCSI_SYNC_CACHE10;
    if(bulk_only_command(selected_device,command,10,0,0,false)) return true;
    device->sync_cache_supported=false;
    klogf(KLOG_WARN,"xhci%u: dev %s rejected SYNCHRONIZE CACHE; WRITE(10) succeeded",
          controller_number,device->info.name);
    return true;
}

const char *xhci_device_name(void){
    return selected_device<slot_count
        && devices[selected_device].kind==XHCI_DEVICE_STORAGE
        ? devices[selected_device].info.name : "none";
}

void xhci_get_probe_stats(struct xhci_probe_stats *stats){
    if(stats) *stats=probe_stats;
}
