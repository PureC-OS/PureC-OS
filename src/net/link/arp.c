#include "arp.h"
#include "../util/byteorder.h"
#include "ethernet.h"
#include "../../drivers/interrupts/timer.h"
#include "../../lib/string.h"

#define ARP_HARDWARE_ETHERNET 1
#define ARP_OPERATION_REQUEST 1
#define ARP_OPERATION_REPLY 2
#define ARP_PACKET_LENGTH 28
#define ARP_CACHE_TTL_MS 120000ULL
#define ARP_PENDING_TTL_MS 5000ULL
#define ARP_REQUEST_INTERVAL_MS 1000ULL

enum arp_entry_state {
    ARP_ENTRY_FREE=0,
    ARP_ENTRY_PENDING=1,
    ARP_ENTRY_VALID=2
};

struct arp_entry {
    struct net_device *device;
    uint32_t ipv4;
    uint8_t mac[6];
    uint64_t updated_ms;
    uint64_t last_request_ms;
    enum arp_entry_state state;
};

struct arp_interface {
    struct net_device *device;
    uint32_t local_ipv4;
};

static struct arp_entry cache[ARP_CACHE_CAPACITY];
static struct arp_interface interfaces[NET_DEVICE_MAX_COUNT];
static struct arp_stats counters;
static volatile bool arp_lock;

static void lock_cache(void){
    while(__atomic_test_and_set(&arp_lock,__ATOMIC_ACQUIRE))
        __asm__ volatile("pause");
}

static void unlock_cache(void){
    __atomic_clear(&arp_lock,__ATOMIC_RELEASE);
}

static bool mac_is_unicast(const uint8_t mac[6]){
    bool all_zero=true;
    for(uint8_t index=0;index<6;index++) if(mac[index]) all_zero=false;
    return !all_zero && !(mac[0]&1U);
}

static struct arp_interface *find_interface(struct net_device *device,
                                            bool allocate){
    struct arp_interface *free_slot=0;
    for(uint8_t index=0;index<NET_DEVICE_MAX_COUNT;index++){
        if(interfaces[index].device==device) return &interfaces[index];
        if(!interfaces[index].device && !free_slot) free_slot=&interfaces[index];
    }
    if(allocate && free_slot) free_slot->device=device;
    return allocate ? free_slot : 0;
}

static struct arp_entry *find_entry(struct net_device *device, uint32_t ipv4){
    for(uint8_t index=0;index<ARP_CACHE_CAPACITY;index++){
        if(cache[index].state!=ARP_ENTRY_FREE && cache[index].device==device
           && cache[index].ipv4==ipv4) return &cache[index];
    }
    return 0;
}

static struct arp_entry *allocate_entry(uint64_t now_ms){
    struct arp_entry *oldest=&cache[0];
    for(uint8_t index=0;index<ARP_CACHE_CAPACITY;index++){
        struct arp_entry *entry=&cache[index];
        if(entry->state==ARP_ENTRY_FREE) return entry;
        uint64_t ttl=entry->state==ARP_ENTRY_VALID
            ? ARP_CACHE_TTL_MS : ARP_PENDING_TTL_MS;
        if(now_ms-entry->updated_ms>=ttl) return entry;
        if(entry->updated_ms<oldest->updated_ms) oldest=entry;
    }
    return oldest;
}

static void learn(struct net_device *device, uint32_t ipv4,
                  const uint8_t mac[6], uint64_t now_ms){
    if(!device || !ipv4 || !mac_is_unicast(mac)) return;
    lock_cache();
    struct arp_interface *interface=find_interface(device,false);
    if(interface && interface->local_ipv4==ipv4
       && memcmp(device->mac,mac,6)!=0){
        counters.address_conflicts++;
        unlock_cache();
        return;
    }
    struct arp_entry *entry=find_entry(device,ipv4);
    if(!entry) entry=allocate_entry(now_ms);
    entry->device=device;
    entry->ipv4=ipv4;
    memcpy(entry->mac,mac,6);
    entry->updated_ms=now_ms;
    entry->last_request_ms=0;
    entry->state=ARP_ENTRY_VALID;
    counters.cache_updates++;
    unlock_cache();
}

static bool send_packet(struct net_device *device, uint16_t operation,
                        uint32_t sender_ipv4, const uint8_t target_mac[6],
                        uint32_t target_ipv4){
    static const uint8_t broadcast[6]={0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    uint8_t packet[ARP_PACKET_LENGTH];
    memset(packet,0,sizeof(packet));
    net_write_be16(packet,ARP_HARDWARE_ETHERNET);
    net_write_be16(packet+2,ETHERNET_TYPE_IPV4);
    packet[4]=6;
    packet[5]=4;
    net_write_be16(packet+6,operation);
    memcpy(packet+8,device->mac,6);
    net_write_be32(packet+14,sender_ipv4);
    if(operation==ARP_OPERATION_REPLY) memcpy(packet+18,target_mac,6);
    net_write_be32(packet+24,target_ipv4);
    const uint8_t *destination=operation==ARP_OPERATION_REQUEST
        ? broadcast : target_mac;
    return ethernet_send(device,destination,ETHERNET_TYPE_ARP,
                         packet,sizeof(packet));
}

static void receive_packet(const struct ethernet_packet *frame){
    const uint8_t *packet=frame->payload;
    if(frame->payload_length<ARP_PACKET_LENGTH
       || net_read_be16(packet)!=ARP_HARDWARE_ETHERNET
       || net_read_be16(packet+2)!=ETHERNET_TYPE_IPV4
       || packet[4]!=6 || packet[5]!=4){
        counters.malformed++;
        return;
    }
    uint16_t operation=net_read_be16(packet+6);
    if(operation!=ARP_OPERATION_REQUEST && operation!=ARP_OPERATION_REPLY){
        counters.malformed++;
        return;
    }
    const uint8_t *sender_mac=packet+8;
    uint32_t sender_ipv4=net_read_be32(packet+14);
    uint32_t target_ipv4=net_read_be32(packet+24);
    if(!mac_is_unicast(sender_mac) || memcmp(sender_mac,frame->source,6)!=0){
        counters.malformed++;
        return;
    }
    if(operation==ARP_OPERATION_REQUEST) counters.requests_received++;
    else counters.replies_received++;
    learn(frame->device,sender_ipv4,sender_mac,timer_ticks());

    uint32_t local_ipv4=arp_get_local_ipv4(frame->device);
    if(operation==ARP_OPERATION_REQUEST && local_ipv4
       && target_ipv4==local_ipv4){
        if(send_packet(frame->device,ARP_OPERATION_REPLY,local_ipv4,
                       sender_mac,sender_ipv4)) counters.replies_sent++;
    }
}

bool arp_init(void){
    memset(cache,0,sizeof(cache));
    memset(interfaces,0,sizeof(interfaces));
    memset(&counters,0,sizeof(counters));
    arp_lock=false;
    return ethernet_register_handler(ETHERNET_TYPE_ARP,receive_packet);
}

bool arp_set_local_ipv4(struct net_device *device, uint32_t address){
    if(!device || !device->registered) return false;
    lock_cache();
    struct arp_interface *interface=find_interface(device,true);
    if(!interface){
        unlock_cache();
        return false;
    }
    interface->local_ipv4=address;
    for(uint8_t index=0;index<ARP_CACHE_CAPACITY;index++){
        if(cache[index].device==device) cache[index].state=ARP_ENTRY_FREE;
    }
    unlock_cache();
    return true;
}

uint32_t arp_get_local_ipv4(struct net_device *device){
    if(!device) return 0;
    lock_cache();
    struct arp_interface *interface=find_interface(device,false);
    uint32_t address=interface ? interface->local_ipv4 : 0;
    unlock_cache();
    return address;
}

bool arp_lookup(struct net_device *device, uint32_t ipv4, uint8_t mac[6]){
    if(!device || !ipv4 || !mac) return false;
    uint64_t now=timer_ticks();
    lock_cache();
    struct arp_entry *entry=find_entry(device,ipv4);
    bool found=entry && entry->state==ARP_ENTRY_VALID
        && now-entry->updated_ms<ARP_CACHE_TTL_MS;
    if(found) memcpy(mac,entry->mac,6);
    else if(entry && entry->state==ARP_ENTRY_VALID){
        entry->state=ARP_ENTRY_FREE;
        counters.cache_expirations++;
    }
    unlock_cache();
    return found;
}

bool arp_resolve(struct net_device *device, uint32_t ipv4, uint8_t mac[6]){
    if(arp_lookup(device,ipv4,mac)) return true;
    uint32_t local_ipv4=arp_get_local_ipv4(device);
    if(!local_ipv4) return false;
    uint64_t now=timer_ticks();
    bool should_request=false;
    lock_cache();
    struct arp_entry *entry=find_entry(device,ipv4);
    if(!entry){
        entry=allocate_entry(now);
        memset(entry,0,sizeof(*entry));
        entry->device=device;
        entry->ipv4=ipv4;
        entry->state=ARP_ENTRY_PENDING;
        entry->updated_ms=now;
        should_request=true;
    } else if(entry->state==ARP_ENTRY_PENDING
              && now-entry->last_request_ms>=ARP_REQUEST_INTERVAL_MS){
        should_request=true;
    }
    if(should_request) entry->last_request_ms=now;
    unlock_cache();
    if(should_request && send_packet(device,ARP_OPERATION_REQUEST,local_ipv4,
                                     0,ipv4)) counters.requests_sent++;
    return false;
}

void arp_poll(uint64_t now_ms){
    lock_cache();
    for(uint8_t index=0;index<ARP_CACHE_CAPACITY;index++){
        struct arp_entry *entry=&cache[index];
        if(entry->state==ARP_ENTRY_FREE) continue;
        uint64_t ttl=entry->state==ARP_ENTRY_VALID
            ? ARP_CACHE_TTL_MS : ARP_PENDING_TTL_MS;
        if(now_ms-entry->updated_ms>=ttl){
            entry->state=ARP_ENTRY_FREE;
            counters.cache_expirations++;
        }
    }
    unlock_cache();
}

uint32_t arp_cache_snapshot(struct arp_cache_record *records,
                            uint32_t capacity){
    if(!records || !capacity) return 0;
    uint64_t now=timer_ticks();
    uint32_t count=0;
    lock_cache();
    for(uint8_t index=0;index<ARP_CACHE_CAPACITY && count<capacity;index++){
        const struct arp_entry *entry=&cache[index];
        if(entry->state==ARP_ENTRY_FREE) continue;
        records[count].device=entry->device;
        records[count].ipv4=entry->ipv4;
        memcpy(records[count].mac,entry->mac,6);
        records[count].age_ms=now-entry->updated_ms;
        records[count].valid=entry->state==ARP_ENTRY_VALID;
        records[count].pending=entry->state==ARP_ENTRY_PENDING;
        count++;
    }
    unlock_cache();
    return count;
}

void arp_get_stats(struct arp_stats *stats){
    if(!stats) return;
    lock_cache();
    *stats=counters;
    unlock_cache();
}
