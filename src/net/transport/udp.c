#include "udp.h"
#include "../util/byteorder.h"
#include "../../lib/string.h"

#define UDP_BINDING_CAPACITY 8

struct udp_binding {
    uint16_t port;
    udp_handler handler;
};

static struct udp_binding bindings[UDP_BINDING_CAPACITY];
static uint8_t transmit_datagram[IPV4_PAYLOAD_MAX_LENGTH];
static volatile bool transmit_lock;

static void receive_datagram(const struct ipv4_packet *packet){
    if(packet->payload_length<UDP_HEADER_LENGTH) return;
    const uint8_t *data=packet->payload;
    uint16_t length=net_read_be16(data+4);
    if(length<UDP_HEADER_LENGTH || length>packet->payload_length) return;
    struct udp_datagram datagram={
        .device=packet->device,
        .source_address=packet->source,
        .destination_address=packet->destination,
        .source_port=net_read_be16(data),
        .destination_port=net_read_be16(data+2),
        .payload=data+UDP_HEADER_LENGTH,
        .payload_length=(uint16_t)(length-UDP_HEADER_LENGTH)
    };
    for(uint8_t index=0;index<UDP_BINDING_CAPACITY;index++){
        if(bindings[index].handler
           && bindings[index].port==datagram.destination_port){
            bindings[index].handler(&datagram);
            return;
        }
    }
}

bool udp_init(void){
    memset(bindings,0,sizeof(bindings));
    transmit_lock=false;
    return ipv4_register_handler(IPV4_PROTOCOL_UDP,receive_datagram);
}

bool udp_bind(uint16_t port, udp_handler handler){
    if(!port || !handler) return false;
    for(uint8_t index=0;index<UDP_BINDING_CAPACITY;index++){
        if(bindings[index].handler && bindings[index].port==port) return false;
    }
    for(uint8_t index=0;index<UDP_BINDING_CAPACITY;index++){
        if(bindings[index].handler) continue;
        bindings[index].port=port;
        bindings[index].handler=handler;
        return true;
    }
    return false;
}

enum ipv4_send_result udp_send_from(struct net_device *device,
                                    uint32_t source_address,
                                    uint32_t destination_address,
                                    uint16_t source_port,
                                    uint16_t destination_port,
                                    const uint8_t *payload, uint16_t length){
    if(!source_port || !destination_port || (length && !payload)
       || length>UDP_PAYLOAD_MAX_LENGTH) return IPV4_SEND_ERROR;
    if(__atomic_test_and_set(&transmit_lock,__ATOMIC_ACQUIRE))
        return IPV4_SEND_ERROR;
    net_write_be16(transmit_datagram,source_port);
    net_write_be16(transmit_datagram+2,destination_port);
    net_write_be16(transmit_datagram+4,(uint16_t)(UDP_HEADER_LENGTH+length));
    net_write_be16(transmit_datagram+6,0);
    if(length) memcpy(transmit_datagram+UDP_HEADER_LENGTH,payload,length);
    enum ipv4_send_result result=ipv4_send_from(
        device,source_address,destination_address,IPV4_PROTOCOL_UDP,
        transmit_datagram,(uint16_t)(UDP_HEADER_LENGTH+length));
    __atomic_clear(&transmit_lock,__ATOMIC_RELEASE);
    return result;
}

enum ipv4_send_result udp_send(struct net_device *device,
                               uint32_t destination_address,
                               uint16_t source_port, uint16_t destination_port,
                               const uint8_t *payload, uint16_t length){
    struct ipv4_interface_config config;
    if(!ipv4_get_config(device,&config)) return IPV4_SEND_ERROR;
    return udp_send_from(device,config.address,destination_address,
                         source_port,destination_port,payload,length);
}
