#include "ipv4.h"
#include "../link/arp.h"
#include "../link/ethernet.h"
#include "../util/byteorder.h"
#include "../../lib/string.h"

#define IPV4_HANDLER_CAPACITY 8
#define IPV4_DEFAULT_TTL 64

struct ipv4_handler_entry {
    uint8_t protocol;
    ipv4_handler handler;
};

static struct ipv4_handler_entry handlers[IPV4_HANDLER_CAPACITY];
static struct ipv4_interface_config interfaces[NET_DEVICE_MAX_COUNT];
static uint8_t transmit_packet[NET_ETHERNET_MTU];
static volatile bool transmit_lock;
static uint16_t next_identification=1;

uint16_t ipv4_checksum(const uint8_t *data, uint16_t length){
    uint32_t sum=0;
    while(length>=2){
        sum+=net_read_be16(data);
        data+=2;
        length=(uint16_t)(length-2);
    }
    if(length) sum+=(uint16_t)data[0]<<8;
    while(sum>>16) sum=(sum&0xFFFFU)+(sum>>16);
    return (uint16_t)~sum;
}

static struct ipv4_interface_config *find_interface(
    struct net_device *device, bool allocate){
    struct ipv4_interface_config *free_slot=0;
    for(uint8_t index=0;index<NET_DEVICE_MAX_COUNT;index++){
        if(interfaces[index].device==device) return &interfaces[index];
        if(!interfaces[index].device && !free_slot) free_slot=&interfaces[index];
    }
    if(allocate && free_slot) free_slot->device=device;
    return allocate ? free_slot : 0;
}

static void receive_packet(const struct ethernet_packet *frame){
    const uint8_t *data=frame->payload;
    if(frame->payload_length<IPV4_HEADER_MIN_LENGTH || (data[0]>>4)!=4){
        frame->device->stats.rx_errors++;
        return;
    }
    uint16_t header_length=(uint16_t)(data[0]&0x0FU)*4U;
    uint16_t total_length=net_read_be16(data+2);
    if(header_length<IPV4_HEADER_MIN_LENGTH || header_length>total_length
       || total_length>frame->payload_length || ipv4_checksum(data,header_length)){
        frame->device->stats.rx_errors++;
        return;
    }
    uint16_t fragment=net_read_be16(data+6);
    if(fragment&0x3FFFU) return;
    uint32_t destination=net_read_be32(data+16);
    struct ipv4_interface_config *interface=find_interface(frame->device,false);
    if(interface && interface->configured){
        uint32_t directed_broadcast=(interface->address&interface->netmask)
            |~interface->netmask;
        if(destination!=IPV4_BROADCAST && destination!=directed_broadcast
           && destination!=interface->address) return;
    }
    struct ipv4_packet packet={
        .device=frame->device,
        .source=net_read_be32(data+12),
        .destination=destination,
        .protocol=data[9],
        .ttl=data[8],
        .payload=data+header_length,
        .payload_length=(uint16_t)(total_length-header_length)
    };
    for(uint8_t index=0;index<IPV4_HANDLER_CAPACITY;index++){
        if(handlers[index].handler && handlers[index].protocol==packet.protocol){
            handlers[index].handler(&packet);
            return;
        }
    }
}

bool ipv4_init(void){
    memset(handlers,0,sizeof(handlers));
    memset(interfaces,0,sizeof(interfaces));
    transmit_lock=false;
    next_identification=1;
    return ethernet_register_handler(ETHERNET_TYPE_IPV4,receive_packet);
}

bool ipv4_register_handler(uint8_t protocol, ipv4_handler handler){
    if(!protocol || !handler) return false;
    for(uint8_t index=0;index<IPV4_HANDLER_CAPACITY;index++){
        if(handlers[index].handler && handlers[index].protocol==protocol)
            return false;
    }
    for(uint8_t index=0;index<IPV4_HANDLER_CAPACITY;index++){
        if(handlers[index].handler) continue;
        handlers[index].protocol=protocol;
        handlers[index].handler=handler;
        return true;
    }
    return false;
}

bool ipv4_configure(struct net_device *device, uint32_t address,
                    uint32_t netmask, uint32_t gateway){
    if(!device || !device->registered || !address || !netmask) return false;
    struct ipv4_interface_config *interface=find_interface(device,true);
    if(!interface || !arp_set_local_ipv4(device,address)) return false;
    interface->address=address;
    interface->netmask=netmask;
    interface->gateway=gateway;
    __atomic_store_n(&interface->configured,true,__ATOMIC_RELEASE);
    return true;
}

bool ipv4_get_config(struct net_device *device,
                     struct ipv4_interface_config *config){
    if(!device || !config) return false;
    struct ipv4_interface_config *interface=find_interface(device,false);
    if(!interface || !__atomic_load_n(&interface->configured,__ATOMIC_ACQUIRE))
        return false;
    *config=*interface;
    return true;
}

enum ipv4_send_result ipv4_send_from(struct net_device *device,
                                     uint32_t source, uint32_t destination,
                                     uint8_t protocol, const uint8_t *payload,
                                     uint16_t length){
    if(!device || !protocol || (length && !payload)
       || length>IPV4_PAYLOAD_MAX_LENGTH) return IPV4_SEND_ERROR;
    struct ipv4_interface_config *interface=find_interface(device,false);
    if(source && (!interface || !interface->configured
                  || source!=interface->address)) return IPV4_SEND_ERROR;
    uint8_t destination_mac[6];
    if(destination==IPV4_BROADCAST){
        memset(destination_mac,0xFF,sizeof(destination_mac));
    } else {
        if(!interface || !interface->configured) return IPV4_SEND_ERROR;
        uint32_t next_hop=(destination&interface->netmask)
                ==(interface->address&interface->netmask)
            ? destination : interface->gateway;
        if(!next_hop) return IPV4_SEND_ERROR;
        if(!arp_resolve(device,next_hop,destination_mac))
            return IPV4_SEND_WAITING_ARP;
    }
    if(__atomic_test_and_set(&transmit_lock,__ATOMIC_ACQUIRE))
        return IPV4_SEND_ERROR;
    transmit_packet[0]=0x45;
    transmit_packet[1]=0;
    net_write_be16(transmit_packet+2,(uint16_t)(IPV4_HEADER_MIN_LENGTH+length));
    net_write_be16(transmit_packet+4,next_identification++);
    net_write_be16(transmit_packet+6,0x4000);
    transmit_packet[8]=IPV4_DEFAULT_TTL;
    transmit_packet[9]=protocol;
    net_write_be16(transmit_packet+10,0);
    net_write_be32(transmit_packet+12,source);
    net_write_be32(transmit_packet+16,destination);
    net_write_be16(transmit_packet+10,
                   ipv4_checksum(transmit_packet,IPV4_HEADER_MIN_LENGTH));
    if(length) memcpy(transmit_packet+IPV4_HEADER_MIN_LENGTH,payload,length);
    bool sent=ethernet_send(device,destination_mac,ETHERNET_TYPE_IPV4,
                            transmit_packet,
                            (uint16_t)(IPV4_HEADER_MIN_LENGTH+length));
    __atomic_clear(&transmit_lock,__ATOMIC_RELEASE);
    return sent ? IPV4_SEND_OK : IPV4_SEND_ERROR;
}

enum ipv4_send_result ipv4_send(struct net_device *device,
                                uint32_t destination, uint8_t protocol,
                                const uint8_t *payload, uint16_t length){
    struct ipv4_interface_config config;
    if(!ipv4_get_config(device,&config)) return IPV4_SEND_ERROR;
    return ipv4_send_from(device,config.address,destination,protocol,
                          payload,length);
}

bool ipv4_parse_address(const char *text, uint32_t *address){
    if(!text || !address) return false;
    uint32_t value=0;
    for(uint8_t part=0;part<4;part++){
        if(*text<'0' || *text>'9') return false;
        uint32_t octet=0;
        uint8_t digits=0;
        while(*text>='0' && *text<='9'){
            octet=octet*10U+(uint32_t)(*text-'0');
            if(octet>255 || ++digits>3) return false;
            text++;
        }
        value=(value<<8)|octet;
        if(part<3){
            if(*text!='.') return false;
            text++;
        }
    }
    if(*text) return false;
    *address=value;
    return true;
}
