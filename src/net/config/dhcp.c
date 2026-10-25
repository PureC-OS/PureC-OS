#include "dhcp.h"
#include "../name/dns.h"
#include "../network/ipv4.h"
#include "../transport/udp.h"
#include "../util/byteorder.h"
#include "../../drivers/interrupts/timer.h"
#include "../../kernel/diagnostics/klog.h"
#include "../../lib/string.h"

#define DHCP_CLIENT_PORT 68
#define DHCP_SERVER_PORT 67
#define DHCP_PACKET_CAPACITY 576
#define DHCP_MAGIC_COOKIE 0x63825363U
#define DHCP_RETRY_MS 4000ULL
#define DHCP_MAX_RETRIES 5

#define DHCP_DISCOVER 1
#define DHCP_OFFER 2
#define DHCP_REQUEST 3
#define DHCP_ACK 5
#define DHCP_NAK 6

enum dhcp_state {
    DHCP_STATE_INIT=0,
    DHCP_STATE_SELECTING=1,
    DHCP_STATE_REQUESTING=2,
    DHCP_STATE_BOUND=3
};

struct dhcp_client {
    struct net_device *device;
    volatile enum dhcp_state state;
    uint32_t transaction;
    uint32_t offered_address;
    uint32_t server;
    uint32_t netmask;
    uint32_t gateway;
    uint32_t dns;
    uint32_t lease_seconds;
    uint64_t last_send_ms;
    uint64_t lease_start_ms;
    uint8_t retries;
};

static struct dhcp_client clients[NET_DEVICE_MAX_COUNT];

static struct dhcp_client *find_client(struct net_device *device){
    for(uint8_t index=0;index<NET_DEVICE_MAX_COUNT;index++)
        if(clients[index].device==device) return &clients[index];
    return 0;
}

static bool append_option(uint8_t *packet, uint16_t *offset, uint8_t code,
                          const void *value, uint8_t length){
    if((uint32_t)*offset+2U+length>=DHCP_PACKET_CAPACITY) return false;
    packet[(*offset)++]=code;
    packet[(*offset)++]=length;
    memcpy(packet+*offset,value,length);
    *offset=(uint16_t)(*offset+length);
    return true;
}

static bool send_message(struct dhcp_client *client, uint8_t message_type){
    uint8_t packet[DHCP_PACKET_CAPACITY];
    memset(packet,0,sizeof(packet));
    packet[0]=1;
    packet[1]=1;
    packet[2]=6;
    net_write_be32(packet+4,client->transaction);
    net_write_be16(packet+10,0x8000);
    memcpy(packet+28,client->device->mac,6);
    net_write_be32(packet+236,DHCP_MAGIC_COOKIE);
    uint16_t offset=240;
    if(!append_option(packet,&offset,53,&message_type,1)) return false;
    if(message_type==DHCP_DISCOVER){
        static const uint8_t parameters[4]={1,3,6,51};
        if(!append_option(packet,&offset,55,parameters,sizeof(parameters)))
            return false;
    } else {
        uint8_t address[4];
        net_write_be32(address,client->offered_address);
        if(!append_option(packet,&offset,50,address,4)) return false;
        net_write_be32(address,client->server);
        if(!append_option(packet,&offset,54,address,4)) return false;
    }
    packet[offset++]=255;
    return udp_send_from(client->device,0,IPV4_BROADCAST,
                         DHCP_CLIENT_PORT,DHCP_SERVER_PORT,
                         packet,offset)==IPV4_SEND_OK;
}

static void parse_options(struct dhcp_client *client, const uint8_t *packet,
                          uint16_t length, uint8_t *message_type){
    uint16_t offset=240;
    while(offset<length){
        uint8_t code=packet[offset++];
        if(code==255) break;
        if(code==0) continue;
        if(offset>=length) break;
        uint8_t size=packet[offset++];
        if(offset+size>length) break;
        if(code==53 && size==1) *message_type=packet[offset];
        else if(code==54 && size==4) client->server=net_read_be32(packet+offset);
        else if(code==1 && size==4) client->netmask=net_read_be32(packet+offset);
        else if(code==3 && size>=4) client->gateway=net_read_be32(packet+offset);
        else if(code==6 && size>=4) client->dns=net_read_be32(packet+offset);
        else if(code==51 && size==4)
            client->lease_seconds=net_read_be32(packet+offset);
        offset=(uint16_t)(offset+size);
    }
}

static void receive_message(const struct udp_datagram *datagram){
    if(datagram->source_port!=DHCP_SERVER_PORT || datagram->payload_length<241)
        return;
    const uint8_t *packet=datagram->payload;
    struct dhcp_client *client=find_client(datagram->device);
    if(!client || packet[0]!=2 || packet[1]!=1 || packet[2]!=6
       || net_read_be32(packet+4)!=client->transaction
       || net_read_be32(packet+236)!=DHCP_MAGIC_COOKIE
       || memcmp(packet+28,client->device->mac,6)!=0) return;
    uint8_t message_type=0;
    parse_options(client,packet,datagram->payload_length,&message_type);
    if(message_type==DHCP_OFFER && client->state==DHCP_STATE_SELECTING){
        client->offered_address=net_read_be32(packet+16);
        if(!client->offered_address || !client->server) return;
        client->state=DHCP_STATE_REQUESTING;
        client->retries=0;
        client->last_send_ms=timer_ticks();
        (void)send_message(client,DHCP_REQUEST);
    } else if(message_type==DHCP_ACK
              && client->state==DHCP_STATE_REQUESTING){
        uint32_t address=net_read_be32(packet+16);
        if(!address) address=client->offered_address;
        if(!client->netmask) client->netmask=0xFFFFFF00U;
        if(!ipv4_configure(client->device,address,client->netmask,
                           client->gateway)) return;
        if(client->dns) (void)dns_set_server(client->device,client->dns);
        client->lease_start_ms=timer_ticks();
        __atomic_store_n(&client->state,DHCP_STATE_BOUND,__ATOMIC_RELEASE);
        klogf(KLOG_OK,"dhcp: %s address=%u.%u.%u.%u gateway=%u.%u.%u.%u",
              client->device->name,(address>>24)&255,(address>>16)&255,
              (address>>8)&255,address&255,(client->gateway>>24)&255,
              (client->gateway>>16)&255,(client->gateway>>8)&255,
              client->gateway&255);
    } else if(message_type==DHCP_NAK){
        client->state=DHCP_STATE_INIT;
        client->retries=0;
    }
}

bool dhcp_init(void){
    memset(clients,0,sizeof(clients));
    return udp_bind(DHCP_CLIENT_PORT,receive_message);
}

bool dhcp_start(struct net_device *device){
    if(!device || !device->registered || find_client(device)) return false;
    for(uint8_t index=0;index<NET_DEVICE_MAX_COUNT;index++){
        if(clients[index].device) continue;
        clients[index].device=device;
        clients[index].transaction=0x50430000U
            |((uint32_t)device->mac[4]<<8)|device->mac[5];
        clients[index].state=DHCP_STATE_INIT;
        return true;
    }
    return false;
}

void dhcp_poll(uint64_t now_ms){
    for(uint8_t index=0;index<NET_DEVICE_MAX_COUNT;index++){
        struct dhcp_client *client=&clients[index];
        if(!client->device || !client->device->cached_link_up) continue;
        if(client->state==DHCP_STATE_BOUND){
            if(client->lease_seconds
               && now_ms-client->lease_start_ms
                  >=(uint64_t)client->lease_seconds*1000ULL){
                client->state=DHCP_STATE_INIT;
                client->retries=0;
            }
            continue;
        }
        if(client->state!=DHCP_STATE_INIT
           && now_ms-client->last_send_ms<DHCP_RETRY_MS) continue;
        if(client->retries>=DHCP_MAX_RETRIES){
            client->state=DHCP_STATE_INIT;
            client->retries=0;
            continue;
        }
        uint8_t type=client->state==DHCP_STATE_REQUESTING
            ? DHCP_REQUEST : DHCP_DISCOVER;
        if(client->state==DHCP_STATE_INIT) client->state=DHCP_STATE_SELECTING;
        client->last_send_ms=now_ms;
        client->retries++;
        (void)send_message(client,type);
    }
}

bool dhcp_is_bound(struct net_device *device){
    struct dhcp_client *client=find_client(device);
    return client && __atomic_load_n(&client->state,__ATOMIC_ACQUIRE)
        ==DHCP_STATE_BOUND;
}
