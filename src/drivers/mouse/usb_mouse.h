#pragma once

#include <stdbool.h>
#include <stdint.h>

struct usb_mouse_info
{
    uint32_t reports;
    uint16_t vendor_id;
    uint16_t product_id;
    uint8_t port;
    bool connected;
};

void usb_mouse_attach(uint16_t vendor_id, uint16_t product_id, uint8_t port);
void usb_mouse_detach(void);
void usb_mouse_report(const uint8_t *report, uint32_t length);
void usb_mouse_poll(void);
struct usb_mouse_info usb_mouse_get_info(void);
