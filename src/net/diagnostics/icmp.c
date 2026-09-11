#include "icmp.h"
#include "../network/ipv4.h"
#include "../util/byteorder.h"
#include "../../drivers/interrupts/timer.h"
#include "../../kernel/process/scheduler.h"
#include "../../lib/string.h"

#define ICMP_ECHO_REPLY 0
#define ICMP_ECHO_REQUEST 8
#define ICMP_ECHO_DATA_LENGTH 56
#define ICMP_ECHO_PACKET_LENGTH (8+ICMP_ECHO_DATA_LENGTH)
#define ICMP_ARP_RETRY_MS 50

struct ping_request {
    struct net_device *device;
    uint32_t address;
    uint16_t identifier;
    uint16_t sequence;
    uint64_t sent_ms;
    volatile bool active;
    volatile bool complete;
    uint8_t ttl;
};

static struct ping_request pending;
static volatile bool ping_lock;
static volatile bool reply_lock;
static uint8_t reply_packet[IPV4_PAYLOAD_MAX_LENGTH];
static uint16_t next_identifier=0x5000;

static void receive_icmp(const struct ipv4_packet *packet){
    if(packet->payload_length<8 || ipv4_checksum(packet->payload,
                                                 packet->payload_length))
        return;
    const uint8_t *data=packet->payload;
    if(data[0]==ICMP_ECHO_REPLY && data[1]==0 && pending.active
       && packet->device==pending.device && packet->source==pending.address
       && net_read_be16(data+4)==pending.identifier
       && net_read_be16(data+6)==pending.sequence){
        pending.ttl=packet->ttl;
        __atomic_thread_fence(__ATOMIC_RELEASE);
        pending.complete=true;
        return;
    }
    if(data[0]!=ICMP_ECHO_REQUEST || data[1]!=0
       || __atomic_test_and_set(&reply_lock,__ATOMIC_ACQUIRE)) return;
    memcpy(reply_packet,data,packet->payload_length);
    reply_packet[0]=ICMP_ECHO_REPLY;
    reply_packet[2]=0;
    reply_packet[3]=0;
    net_write_be16(reply_packet+2,
                   ipv4_checksum(reply_packet,packet->payload_length));
    (void)ipv4_send(packet->device,packet->source,IPV4_PROTOCOL_ICMP,
                    reply_packet,packet->payload_length);
    __atomic_clear(&reply_lock,__ATOMIC_RELEASE);
}

bool icmp_init(void){
    memset(&pending,0,sizeof(pending));
    ping_lock=false;
    reply_lock=false;
    return ipv4_register_handler(IPV4_PROTOCOL_ICMP,receive_icmp);
}

enum icmp_ping_status icmp_ping(struct net_device *device, uint32_t address,
                                uint16_t sequence, uint32_t timeout_ms,
                                struct icmp_ping_reply *reply){
    if(!device || !address || !timeout_ms || !reply) return ICMP_PING_NETWORK;
    if(__atomic_test_and_set(&ping_lock,__ATOMIC_ACQUIRE)) return ICMP_PING_BUSY;
    uint8_t echo[ICMP_ECHO_PACKET_LENGTH];
    memset(echo,0,sizeof(echo));
    uint16_t identifier=++next_identifier;
    echo[0]=ICMP_ECHO_REQUEST;
    net_write_be16(echo+4,identifier);
    net_write_be16(echo+6,sequence);
    for(uint16_t index=8;index<sizeof(echo);index++)
        echo[index]=(uint8_t)(index-8);
    net_write_be16(echo+2,ipv4_checksum(echo,sizeof(echo)));
    memset(&pending,0,sizeof(pending));
    pending.device=device;
    pending.address=address;
    pending.identifier=identifier;
    pending.sequence=sequence;
    pending.active=true;
    uint64_t start=timer_ticks();
    bool sent=false;
    while(timer_ticks()-start<timeout_ms){
        if(!sent){
            enum ipv4_send_result result=ipv4_send(
                device,address,IPV4_PROTOCOL_ICMP,echo,sizeof(echo));
            if(result==IPV4_SEND_ERROR){
                pending.active=false;
                __atomic_clear(&ping_lock,__ATOMIC_RELEASE);
                return ICMP_PING_NETWORK;
            }
            if(result==IPV4_SEND_OK){
                pending.sent_ms=timer_ticks();
                sent=true;
            }
        }
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if(pending.complete){
            uint64_t elapsed=timer_ticks()-pending.sent_ms;
            reply->address=address;
            reply->round_trip_ms=elapsed>UINT32_MAX ? UINT32_MAX
                                                    : (uint32_t)elapsed;
            reply->sequence=sequence;
            reply->ttl=pending.ttl;
            pending.active=false;
            __atomic_clear(&ping_lock,__ATOMIC_RELEASE);
            return ICMP_PING_OK;
        }
        scheduler_sleep(ICMP_ARP_RETRY_MS);
    }
    pending.active=false;
    __atomic_clear(&ping_lock,__ATOMIC_RELEASE);
    return ICMP_PING_TIMEOUT;
}
