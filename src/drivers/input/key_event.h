#pragma once
#include <stdint.h>
#include <stdbool.h>

enum key_mod {
    KEY_MOD_LSHIFT = 1 << 0,
    KEY_MOD_LCTRL  = 1 << 1,
    KEY_MOD_LALT   = 1 << 2,
    KEY_MOD_LGUI   = 1 << 3,
    KEY_MOD_RSHIFT = 1 << 4,
    KEY_MOD_RCTRL  = 1 << 5,
    KEY_MOD_RALT   = 1 << 6,
    KEY_MOD_RGUI   = 1 << 7
};

struct key_event {
    uint16_t scancode;
    uint8_t pressed;
    uint8_t repeat;
    uint8_t mods;
    uint8_t reserved[3];
};
