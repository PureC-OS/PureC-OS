#pragma once

#include <stdbool.h>
#include <stdint.h>

void clock_app_open(void);
void clock_app_draw(uint32_t window_x, uint32_t window_y);
bool clock_app_handle_key(char key);
