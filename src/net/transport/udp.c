#include "udp.h"
#include "udp_endpoint.h"
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
    if(net_read_be16(data+6)){
        uint32_t sum=0;
        sum+=(packet->source>>16)&0xFFFF; sum+=packet->source&0xFFFF;
        sum+=(packet->destination>>16)&0xFFFF; sum+=packet->destination&0xFFFF;
        sum+=IPV4_PROTOCOL_UDP; sum+=length;
        for(uint16_t i=0;i+1<length;i+=2) sum+=(uint32_t)((data[i]<<8)|data[i+1]);
        if(length&1) sum+=(uint32_t)(data[length-1]<<8);
        while(sum>>16) sum=(sum&0xFFFF)+(sum>>16);
        if((uint16_t)sum!=0xFFFF) return;
    }
    for(uint8_t index=0;index<UDP_BINDING_CAPACITY;index++){
        if(bindings[index].handler
           && bindings[index].port==datagram.destination_port){
            bindings[index].handler(&datagram);
            return;
        }
    }
    (void)udpe_deliver(&datagram);
}

bool udp_binding_port(uint32_t index, uint16_t *port){
    if(index>=UDP_BINDING_CAPACITY || !bindings[index].handler) return false;
    *port=bindings[index].port;
    return true;
}

bool udp_unbind(uint16_t port){
    for(uint8_t index=0;index<UDP_BINDING_CAPACITY;index++){
        if(bindings[index].handler && bindings[index].port==port){
            bindings[index].handler=0;
            bindings[index].port=0;
            return true;
        }
    }
    return false;
}

bool udp_port_bound(uint16_t port){
    for(uint8_t index=0;index<UDP_BINDING_CAPACITY;index++)
        if(bindings[index].handler && bindings[index].port==port) return true;
    return false;
}

static uint16_t udp_checksum(uint32_t source, uint32_t destination,
                             const uint8_t *datagram, uint16_t length){
    uint32_t sum=0;
    sum+=(source>>16)&0xFFFF; sum+=source&0xFFFF;
    sum+=(destination>>16)&0xFFFF; sum+=destination&0xFFFF;
    sum+=IPV4_PROTOCOL_UDP; sum+=length;
    for(uint16_t i=0;i+1<length;i+=2) sum+=(uint32_t)((datagram[i]<<8)|datagram[i+1]);
    if(length&1) sum+=(uint32_t)(datagram[length-1]<<8);
    while(sum>>16) sum=(sum&0xFFFF)+(sum>>16);
    uint16_t result=(uint16_t)~sum;
    return result ? result : 0xFFFF;
}

bool udp_init(void){
    memset(bindings,0,sizeof(bindings));
    transmit_lock=false;
    return ipv4_register_handler(IPV4_PROTOCOL_UDP,receive_datagram);
}

bool udp_bind(uint16_t port, udp_handler handler){
    if(!port || !handler || udpe_port_in_use(port)) return false;
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
    net_write_be16(transmit_datagram+6,
                   udp_checksum(source_address,destination_address,
                                transmit_datagram,(uint16_t)(UDP_HEADER_LENGTH+length)));
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
