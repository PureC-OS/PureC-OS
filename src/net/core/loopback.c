#include "loopback.h"
#include "../network/ipv4.h"
#include "../../kernel/diagnostics/klog.h"
#include "../../lib/string.h"

static struct net_device loopback;

static bool loopback_transmit(void *context, const uint8_t *frame, uint16_t length){
    (void)context;
    return net_device_receive(&loopback,frame,length);
}

static void loopback_poll(void *context, uint32_t budget){
    (void)context;
    (void)budget;
}

static bool loopback_link_up(void *context){
    (void)context;
    return true;
}

static const struct net_device_ops loopback_ops={
    .transmit=loopback_transmit,
    .poll=loopback_poll,
    .link_up=loopback_link_up
};

bool loopback_init(void){
    memset(&loopback,0,sizeof(loopback));
    strncpy(loopback.name,"lo",sizeof(loopback.name)-1);
    loopback.mac[0]=0x02;
    loopback.mac[5]=0x01;
    loopback.mtu=NET_ETHERNET_MTU;
    loopback.ops=&loopback_ops;
    loopback.loopback=true;
    if(!net_device_register(&loopback)){
        klog(KLOG_WARN,"net: cannot register loopback device");
        return false;
    }
    if(!ipv4_configure(&loopback,0x7F000001U,0xFF000000U,0)){
        klog(KLOG_WARN,"net: cannot configure 127.0.0.1 on lo");
        return false;
    }
    klog(KLOG_OK,"net: lo 127.0.0.1/8 up");
    return true;
}
