#include "dns.h"
#include "../../lib/string.h"

struct dns_server_entry {
    struct net_device *device;
    uint32_t address;
};

static struct dns_server_entry servers[NET_DEVICE_MAX_COUNT];

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

bool dns_init(void){
    memset(servers,0,sizeof(servers));
    return true;
}

bool dns_set_server(struct net_device *device, uint32_t server){
    if(!device || !server) return false;
    struct dns_server_entry *entry=server_entry(device,true);
    if(!entry) return false;
    __atomic_store_n(&entry->address,server,__ATOMIC_RELEASE);
    return true;
}

uint32_t dns_get_server(struct net_device *device){
    if(!device) return 0;
    struct dns_server_entry *entry=server_entry(device,false);
    if(!entry) return 0;
    return __atomic_load_n(&entry->address,__ATOMIC_ACQUIRE);
}
