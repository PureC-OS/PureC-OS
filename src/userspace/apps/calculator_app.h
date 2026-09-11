#pragma once

#include <stdbool.h>
#include <stdint.h>

void calculator_app_open(void);
void calculator_app_draw(uint32_t window_x, uint32_t window_y);
bool calculator_app_handle_key(char key);
