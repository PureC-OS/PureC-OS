#pragma once
#include "../types.hpp"

extern "C" {
#include "../../../../../libgui/include/puregui.h"
}

void hex_handle_hex(char c);
void hex_handle_ascii(char c);
bool hex_handle_special(uint8_t k);
bool hex_handle_mouse(const pg_event* ev);
bool hex_handle_prompt_key(int32_t k);
