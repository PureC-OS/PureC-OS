#pragma once
#include <stdint.h>

#define HID_KEY_LCTRL 224
#define HID_KEY_RGUI  231

uint16_t hid_from_set1(uint8_t code, int extended);
uint16_t hid_from_set2(uint8_t code, int extended);
uint8_t hid_modifier_bit(uint16_t scancode);
