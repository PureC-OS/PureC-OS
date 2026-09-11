#include "net_device.h"
#include "../../lib/string.h"

struct net_rx_queue {
    struct net_frame frames[NET_DEVICE_RX_QUEUE_LENGTH];
    uint8_t read_index;
    uint8_t write_index;
    uint8_t count;
    volatile bool locked;
};

static struct net_device *devices[NET_DEVICE_MAX_COUNT];
static struct net_rx_queue rx_queues[NET_DEVICE_MAX_COUNT];
static uint32_t device_count;

static void queue_lock(struct net_rx_queue *queue){
    while(__atomic_test_and_set(&queue->locked,__ATOMIC_ACQUIRE))
        __asm__ volatile("pause");
}

static void queue_unlock(struct net_rx_queue *queue){
    __atomic_clear(&queue->locked,__ATOMIC_RELEASE);
}

static int32_t device_index(const struct net_device *device){
    for(uint32_t index=0;index<device_count;index++){
        if(devices[index]==device) return (int32_t)index;
    }
    return -1;
}

void net_device_registry_init(void){
    memset(devices,0,sizeof(devices));
    memset(rx_queues,0,sizeof(rx_queues));
    device_count=0;
}

bool net_device_register(struct net_device *device){
    if(!device || !device->ops || !device->ops->transmit
       || !device->ops->poll || device->registered
       || device_count>=NET_DEVICE_MAX_COUNT) return false;
    if(!device->mtu || device->mtu>NET_ETHERNET_MTU)
        device->mtu=NET_ETHERNET_MTU;
    devices[device_count++]=device;
    device->registered=true;
    return true;
}

uint32_t net_device_count(void){ return device_count; }

struct net_device *net_device_get(uint32_t index){
    return index<device_count ? devices[index] : 0;
}

bool net_device_transmit(struct net_device *device,
                         const uint8_t *frame, uint16_t length){
    if(!device || !device->registered || !frame || length<14
       || length>NET_ETHERNET_MAX_FRAME_SIZE){
        if(device) device->stats.tx_dropped++;
        return false;
    }
    if(!device->ops->transmit(device->driver_context,frame,length)){
        device->stats.tx_dropped++;
        return false;
    }
    device->stats.tx_packets++;
    device->stats.tx_bytes+=length;
    return true;
}

void net_device_poll_all(uint32_t budget_per_device){
    if(!budget_per_device) return;
    for(uint32_t index=0;index<device_count;index++){
        struct net_device *device=devices[index];
        device->ops->poll(device->driver_context,budget_per_device);
        if(device->ops->link_up)
            device->cached_link_up=device->ops->link_up(device->driver_context);
    }
}

bool net_device_receive(struct net_device *device,
                        const uint8_t *frame, uint16_t length){
    int32_t index=device_index(device);
    if(index<0 || !frame || length<14 || length>NET_ETHERNET_MAX_FRAME_SIZE){
        if(device) device->stats.rx_errors++;
        return false;
    }
    struct net_rx_queue *queue=&rx_queues[index];
    queue_lock(queue);
    if(queue->count==NET_DEVICE_RX_QUEUE_LENGTH){
        queue_unlock(queue);
        device->stats.rx_dropped++;
        return false;
    }
    struct net_frame *destination=&queue->frames[queue->write_index];
    destination->length=length;
    memcpy(destination->data,frame,length);
    queue->write_index=(uint8_t)((queue->write_index+1)
                                 %NET_DEVICE_RX_QUEUE_LENGTH);
    queue->count++;
    queue_unlock(queue);
    device->stats.rx_packets++;
    device->stats.rx_bytes+=length;
    return true;
}

bool net_device_dequeue(struct net_device *device, struct net_frame *frame){
    int32_t index=device_index(device);
    if(index<0 || !frame) return false;
    struct net_rx_queue *queue=&rx_queues[index];
    queue_lock(queue);
    if(!queue->count){
        queue_unlock(queue);
        return false;
    }
    *frame=queue->frames[queue->read_index];
    queue->read_index=(uint8_t)((queue->read_index+1)
                                %NET_DEVICE_RX_QUEUE_LENGTH);
    queue->count--;
    queue_unlock(queue);
    return true;
}
