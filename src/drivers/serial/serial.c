#include <arch/x86_64/include/portio.h>
#include "serial.h"
#include <stdint.h>

#define COM1 0x3F8

void serial_init(void)
{
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x03);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);
}

static int serial_transmit_empty(void)
{
    return inb(COM1 + 5) & 0x20;
}

void serial_putc(char c)
{
    while (serial_transmit_empty() == 0)
        ;
    outb(COM1, c);
    if (c == '\n')
    {
        while (serial_transmit_empty() == 0)
            ;
        outb(COM1, '\r');
    }
}

void serial_write_string(const char *s)
{
    while (*s)
        serial_putc(*s++);
}
