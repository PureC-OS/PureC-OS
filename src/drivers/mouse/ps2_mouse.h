#pragma once
#include <stdint.h>
#include <stdbool.h>

struct mouse_state {
    int32_t x, y;
    int32_t dx, dy;
    uint8_t buttons;
    bool has_data;
};

struct mouse_debug_state {
    uint32_t irq_count;
    uint32_t poll_count;
    uint32_t packet_count;
    uint8_t controller_status;
    uint8_t last_byte;
    uint8_t reset_ack;
    uint8_t enable_ack;
    bool initialized;
    bool enabled;
    bool interrupts_enabled;
};

void ps2_mouse_init(void);
void ps2_mouse_handler(void);
void ps2_mouse_poll(void);
void mouse_redraw(void);
void mouse_begin_framebuffer_update(void);
void mouse_end_framebuffer_update(void);
void mouse_set_debug_overlay(bool enabled);
bool mouse_get_debug_overlay(void);
struct mouse_state mouse_get_state(void);
struct mouse_debug_state mouse_get_debug_state(void);
void mouse_set_bounds(int32_t w, int32_t h);
void mouse_handle_relative(uint8_t buttons, int8_t dx, int8_t dy);