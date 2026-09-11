#include "ping.h"
#include "../config/dhcp.h"
#include "../core/net_device.h"
#include "../diagnostics/icmp.h"
#include "../name/dns.h"
#include "../network/ipv4.h"

static bool extract_host(const char *target, char host[DNS_HOSTNAME_CAPACITY]){
    if(!target || !*target) return false;
    const char *start=target;
    const char *cursor=target;
    while(*cursor && !(cursor[0]==':' && cursor[1]=='/' && cursor[2]=='/'))
        cursor++;
    if(*cursor) start=cursor+3;
    else if(start[0]=='/' && start[1]=='/') start+=2;
    uint32_t length=0;
    while(start[length] && start[length]!='/' && start[length]!=':'
          && start[length]!='?' && start[length]!='#'
          && start[length]!=' ' && start[length]!='\t'){
        if(length+1>=DNS_HOSTNAME_CAPACITY) return false;
        host[length]=start[length];
        length++;
    }
    host[length]='\0';
    return length!=0;
}

enum net_ping_status net_ping_target(const char *target, uint16_t sequence,
                                     uint32_t timeout_ms,
                                     struct net_ping_reply *reply){
    if(!reply || timeout_ms<100) return NET_PING_INVALID;
    char host[DNS_HOSTNAME_CAPACITY];
    if(!extract_host(target,host)) return NET_PING_INVALID;
    struct net_device *device=0;
    for(uint32_t index=0;index<net_device_count();index++){
        struct net_device *candidate=net_device_get(index);
        if(candidate && candidate->cached_link_up){ device=candidate; break; }
    }
    if(!device){
        for(uint32_t index=0;index<net_device_count();index++){
            struct net_device *candidate=net_device_get(index);
            if(candidate){ device=candidate; break; }
        }
    }
    if(!device) return NET_PING_NO_INTERFACE;
    struct ipv4_interface_config config;
    if(!ipv4_get_config(device,&config) || !dhcp_is_bound(device))
        return NET_PING_NOT_CONFIGURED;
    uint32_t address;
    enum dns_result resolved=dns_resolve_ipv4(device,host,timeout_ms,&address);
    if(resolved==DNS_RESULT_BUSY) return NET_PING_BUSY;
    if(resolved!=DNS_RESULT_OK) return NET_PING_RESOLVE_FAILED;
    struct icmp_ping_reply icmp_reply;
    enum icmp_ping_status status=icmp_ping(
        device,address,sequence,timeout_ms,&icmp_reply);
    if(status==ICMP_PING_TIMEOUT) return NET_PING_TIMEOUT;
    if(status==ICMP_PING_BUSY) return NET_PING_BUSY;
    if(status!=ICMP_PING_OK) return NET_PING_NETWORK_ERROR;
    reply->address=icmp_reply.address;
    reply->round_trip_ms=icmp_reply.round_trip_ms;
    reply->sequence=icmp_reply.sequence;
    reply->ttl=icmp_reply.ttl;
    return NET_PING_OK;
}
