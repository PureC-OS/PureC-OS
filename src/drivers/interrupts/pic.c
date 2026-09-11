#include "pic.h"
#include <stdint.h>

static inline void outb(uint16_t port, uint8_t val){ __asm__ volatile("outb %0,%1"::"a"(val),"Nd"(port)); }
static inline uint8_t inb(uint16_t port){ uint8_t r; __asm__ volatile("inb %1,%0":"=a"(r):"Nd"(port)); return r; }
static inline void io_wait(void){ outb(0x80,0); }

void pic_remap(uint8_t offset1, uint8_t offset2){
    outb(0x20,0x11); io_wait();
    outb(0xA0,0x11); io_wait();
    outb(0x21,offset1); io_wait();
    outb(0xA1,offset2); io_wait();
    outb(0x21,0x04); io_wait();
    outb(0xA1,0x02); io_wait();
    outb(0x21,0x01); io_wait();
    outb(0xA1,0x01); io_wait();
}

void pic_mask_all(void){
    outb(0x21,0xFF);
    outb(0xA1,0xFF);
}
