#include "ethernet.h"
#include "../util/byteorder.h"
#include "../../lib/string.h"

#define ETHERNET_HANDLER_CAPACITY 8
#define ETHERNET_TYPE_MINIMUM 0x0600

struct ethernet_handler_entry {
    uint16_t ethertype;
    ethernet_handler handler;
};

static struct ethernet_handler_entry handlers[ETHERNET_HANDLER_CAPACITY];
static struct ethernet_stats counters;
static volatile bool transmit_lock;
static uint8_t transmit_frame[NET_ETHERNET_MAX_FRAME_SIZE];

static bool mac_is_valid_destination(const uint8_t mac[6]){
    bool all_zero=true;
    for(uint8_t index=0;index<6;index++){
        if(mac[index]){
            all_zero=false;
            break;
        }
    }
    return !all_zero;
}

void ethernet_init(void){
    memset(handlers,0,sizeof(handlers));
    memset(&counters,0,sizeof(counters));
    transmit_lock=false;
}

bool ethernet_register_handler(uint16_t ethertype, ethernet_handler handler){
    if(ethertype<ETHERNET_TYPE_MINIMUM || !handler) return false;
    for(uint8_t index=0;index<ETHERNET_HANDLER_CAPACITY;index++){
        if(handlers[index].handler && handlers[index].ethertype==ethertype)
            return false;
    }
    for(uint8_t index=0;index<ETHERNET_HANDLER_CAPACITY;index++){
        if(handlers[index].handler) continue;
        handlers[index].ethertype=ethertype;
        handlers[index].handler=handler;
        return true;
    }
    return false;
}

bool ethernet_receive(struct net_device *device,
                      const uint8_t *frame, uint16_t length){
    if(!device || !frame || length<ETHERNET_HEADER_LENGTH){
        counters.malformed++;
        return false;
    }
    uint16_t ethertype=net_read_be16(frame+12);
    if(ethertype<ETHERNET_TYPE_MINIMUM){
        counters.malformed++;
        return false;
    }
    struct ethernet_packet packet={
        .device=device,
        .ethertype=ethertype,
        .payload=frame+ETHERNET_HEADER_LENGTH,
        .payload_length=(uint16_t)(length-ETHERNET_HEADER_LENGTH)
    };
    memcpy(packet.destination,frame,6);
    memcpy(packet.source,frame+6,6);
    counters.received++;
    for(uint8_t index=0;index<ETHERNET_HANDLER_CAPACITY;index++){
        if(handlers[index].handler && handlers[index].ethertype==ethertype){
            handlers[index].handler(&packet);
            return true;
        }
    }
    counters.unsupported++;
    return false;
}

bool ethernet_send(struct net_device *device, const uint8_t destination[6],
                   uint16_t ethertype, const uint8_t *payload,
                   uint16_t payload_length){
    if(!device || !destination || !mac_is_valid_destination(destination)
       || ethertype<ETHERNET_TYPE_MINIMUM
       || (payload_length && !payload) || payload_length>device->mtu){
        counters.transmit_errors++;
        return false;
    }
    if(__atomic_test_and_set(&transmit_lock,__ATOMIC_ACQUIRE)){
        counters.transmit_errors++;
        return false;
    }
    memcpy(transmit_frame,destination,6);
    memcpy(transmit_frame+6,device->mac,6);
    net_write_be16(transmit_frame+12,ethertype);
    if(payload_length)
        memcpy(transmit_frame+ETHERNET_HEADER_LENGTH,payload,payload_length);
    bool sent=net_device_transmit(
        device,transmit_frame,(uint16_t)(ETHERNET_HEADER_LENGTH+payload_length));
    __atomic_clear(&transmit_lock,__ATOMIC_RELEASE);
    if(sent) counters.transmitted++;
    else counters.transmit_errors++;
    return sent;
}

void ethernet_get_stats(struct ethernet_stats *stats){
    if(stats) *stats=counters;
}
