#include "dns.h"
#include "../network/ipv4.h"
#include "../transport/udp.h"
#include "../util/byteorder.h"
#include "../../drivers/interrupts/timer.h"
#include "../../kernel/process/scheduler.h"
#include "../../lib/string.h"

#define DNS_CLIENT_PORT 53000
#define DNS_SERVER_PORT 53
#define DNS_PACKET_CAPACITY 512
#define DNS_CACHE_CAPACITY 8
#define DNS_CACHE_NAME_CAPACITY 64
#define DNS_RETRY_MS 250

struct dns_server_entry {
    struct net_device *device;
    uint32_t address;
};

struct dns_cache_entry {
    char name[DNS_CACHE_NAME_CAPACITY];
    uint32_t address;
    uint64_t expires_ms;
};

struct dns_query {
    struct net_device *device;
    uint32_t server;
    uint16_t identifier;
    volatile bool active;
    volatile bool complete;
    volatile bool found;
    uint32_t address;
    uint32_t ttl_seconds;
};

static struct dns_server_entry servers[NET_DEVICE_MAX_COUNT];
static struct dns_cache_entry cache[DNS_CACHE_CAPACITY];
static struct dns_query query;
static volatile bool query_lock;
static uint16_t next_identifier=0x4100;

static struct dns_server_entry *server_entry(struct net_device *device,
                                             bool allocate){
    struct dns_server_entry *free_slot=0;
    for(uint8_t index=0;index<NET_DEVICE_MAX_COUNT;index++){
        if(servers[index].device==device) return &servers[index];
        if(!servers[index].device && !free_slot) free_slot=&servers[index];
    }
    if(allocate && free_slot) free_slot->device=device;
    return allocate ? free_slot : 0;
}

static bool normalize_hostname(const char *source, char *destination,
                               uint32_t capacity){
    if(!source || !*source || !destination || capacity<2) return false;
    uint32_t length=0;
    uint8_t label_length=0;
    while(*source){
        char character=*source++;
        if(character>='A' && character<='Z') character=(char)(character+32);
        if(character=='.'){
            if(!label_length || label_length>63 || length+1>=capacity)
                return false;
            destination[length++]=character;
            label_length=0;
            continue;
        }
        bool valid=(character>='a' && character<='z')
            || (character>='0' && character<='9') || character=='-';
        if(!valid || length+1>=capacity || ++label_length>63) return false;
        destination[length++]=character;
    }
    if(!label_length) return false;
    destination[length]='\0';
    return true;
}

static bool cache_lookup(const char *name, uint32_t *address){
    uint64_t now=timer_ticks();
    for(uint8_t index=0;index<DNS_CACHE_CAPACITY;index++){
        if(cache[index].address && cache[index].expires_ms>now
           && strcmp(cache[index].name,name)==0){
            *address=cache[index].address;
            return true;
        }
    }
    return false;
}

static void cache_store(const char *name, uint32_t address,
                        uint32_t ttl_seconds){
    if(!address || !ttl_seconds) return;
    uint64_t now=timer_ticks();
    struct dns_cache_entry *slot=&cache[0];
    for(uint8_t index=0;index<DNS_CACHE_CAPACITY;index++){
        if(!cache[index].address || cache[index].expires_ms<=now
           || strcmp(cache[index].name,name)==0){
            slot=&cache[index];
            break;
        }
        if(cache[index].expires_ms<slot->expires_ms) slot=&cache[index];
    }
    strncpy(slot->name,name,sizeof(slot->name)-1);
    slot->name[sizeof(slot->name)-1]='\0';
    slot->address=address;
    uint64_t ttl_ms=(uint64_t)ttl_seconds*1000ULL;
    slot->expires_ms=UINT64_MAX-now<ttl_ms ? UINT64_MAX : now+ttl_ms;
}

static bool skip_name(const uint8_t *packet, uint16_t length,
                      uint16_t *offset){
    uint16_t cursor=*offset;
    for(uint8_t labels=0;labels<128;labels++){
        if(cursor>=length) return false;
        uint8_t size=packet[cursor++];
        if(!size){ *offset=cursor; return true; }
        if((size&0xC0U)==0xC0U){
            if(cursor>=length) return false;
            *offset=(uint16_t)(cursor+1);
            return true;
        }
        if(size>63 || cursor+size>length) return false;
        cursor=(uint16_t)(cursor+size);
    }
    return false;
}

static void receive_response(const struct udp_datagram *datagram){
    const uint8_t *packet=datagram->payload;
    uint16_t length=datagram->payload_length;
    if(!query.active || datagram->device!=query.device
       || datagram->source_address!=query.server
       || datagram->source_port!=DNS_SERVER_PORT || length<12
       || net_read_be16(packet)!=query.identifier) return;
    uint16_t flags=net_read_be16(packet+2);
    if(!(flags&0x8000U) || (flags&0x000FU)){
        query.complete=true;
        return;
    }
    uint16_t questions=net_read_be16(packet+4);
    uint16_t answers=net_read_be16(packet+6);
    uint16_t offset=12;
    for(uint16_t index=0;index<questions;index++){
        if(!skip_name(packet,length,&offset) || offset+4>length){
            query.complete=true;
            return;
        }
        offset=(uint16_t)(offset+4);
    }
    for(uint16_t index=0;index<answers;index++){
        if(!skip_name(packet,length,&offset) || offset+10>length) break;
        uint16_t type=net_read_be16(packet+offset);
        uint16_t class_code=net_read_be16(packet+offset+2);
        uint32_t ttl=net_read_be32(packet+offset+4);
        uint16_t data_length=net_read_be16(packet+offset+8);
        offset=(uint16_t)(offset+10);
        if(offset+data_length>length) break;
        if(type==1 && class_code==1 && data_length==4){
            query.address=net_read_be32(packet+offset);
            query.ttl_seconds=ttl;
            query.found=true;
            query.complete=true;
            return;
        }
        offset=(uint16_t)(offset+data_length);
    }
    query.complete=true;
}

bool dns_init(void){
    memset(servers,0,sizeof(servers));
    memset(cache,0,sizeof(cache));
    memset(&query,0,sizeof(query));
    query_lock=false;
    return udp_bind(DNS_CLIENT_PORT,receive_response);
}

bool dns_set_server(struct net_device *device, uint32_t server){
    if(!device || !server) return false;
    struct dns_server_entry *entry=server_entry(device,true);
    if(!entry) return false;
    __atomic_store_n(&entry->address,server,__ATOMIC_RELEASE);
    return true;
}

enum dns_result dns_resolve_ipv4(struct net_device *device,
                                 const char *hostname, uint32_t timeout_ms,
                                 uint32_t *address){
    if(!device || !hostname || !address || timeout_ms<100) return DNS_RESULT_INVALID;
    if(ipv4_parse_address(hostname,address)) return DNS_RESULT_OK;
    char normalized[DNS_HOSTNAME_CAPACITY];
    if(!normalize_hostname(hostname,normalized,sizeof(normalized)))
        return DNS_RESULT_INVALID;
    if(cache_lookup(normalized,address)) return DNS_RESULT_OK;
    struct dns_server_entry *server=server_entry(device,false);
    if(!server) return DNS_RESULT_NO_SERVER;
    uint32_t server_address=__atomic_load_n(&server->address,__ATOMIC_ACQUIRE);
    if(!server_address) return DNS_RESULT_NO_SERVER;
    if(__atomic_test_and_set(&query_lock,__ATOMIC_ACQUIRE)) return DNS_RESULT_BUSY;

    uint8_t packet[DNS_PACKET_CAPACITY];
    memset(packet,0,sizeof(packet));
    uint16_t identifier=++next_identifier;
    net_write_be16(packet,identifier);
    net_write_be16(packet+2,0x0100);
    net_write_be16(packet+4,1);
    uint16_t offset=12;
    const char *label=normalized;
    while(*label){
        const char *end=label;
        while(*end && *end!='.') end++;
        uint16_t size=(uint16_t)(end-label);
        if((uint32_t)offset+1U+size+5U>sizeof(packet)){
            __atomic_clear(&query_lock,__ATOMIC_RELEASE);
            return DNS_RESULT_INVALID;
        }
        packet[offset++]=(uint8_t)size;
        memcpy(packet+offset,label,size);
        offset=(uint16_t)(offset+size);
        label=*end ? end+1 : end;
    }
    packet[offset++]=0;
    net_write_be16(packet+offset,1);
    net_write_be16(packet+offset+2,1);
    offset=(uint16_t)(offset+4);

    memset(&query,0,sizeof(query));
    query.device=device;
    query.server=server_address;
    query.identifier=identifier;
    query.active=true;
    uint64_t start=timer_ticks();
    uint64_t last_send=UINT64_MAX;
    enum dns_result result=DNS_RESULT_TIMEOUT;
    while(timer_ticks()-start<timeout_ms){
        uint64_t now=timer_ticks();
        if(last_send==UINT64_MAX || now-last_send>=DNS_RETRY_MS){
            enum ipv4_send_result sent=udp_send(
                device,server_address,DNS_CLIENT_PORT,DNS_SERVER_PORT,
                packet,offset);
            if(sent==IPV4_SEND_ERROR){ result=DNS_RESULT_NETWORK; break; }
            last_send=now;
        }
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if(query.complete){
            if(query.found){
                *address=query.address;
                cache_store(normalized,query.address,query.ttl_seconds);
                result=DNS_RESULT_OK;
            } else result=DNS_RESULT_NOT_FOUND;
            break;
        }
        scheduler_sleep(10);
    }
    query.active=false;
    __atomic_clear(&query_lock,__ATOMIC_RELEASE);
    return result;
}
