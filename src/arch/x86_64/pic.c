#include <arch/arch.h>
#include <arch/x86_64/include/portio.h>
#include <arch/x86_64/pic.h>
#include <stdint.h>

void pic_remap(uint8_t offset1, uint8_t offset2)
{
    outb(0x20, 0x11);
    io_wait();
    outb(0xA0, 0x11);
    io_wait();
    outb(0x21, offset1);
    io_wait();
    outb(0xA1, offset2);
    io_wait();
    outb(0x21, 0x04);
    io_wait();
    outb(0xA1, 0x02);
    io_wait();
    outb(0x21, 0x01);
    io_wait();
    outb(0xA1, 0x01);
    io_wait();
}

void pic_mask_all(void)
{
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);
}

void arch_interrupts_early_init(void) {
  pic_remap(0x20, 0x28);
  pic_mask_all();
}
