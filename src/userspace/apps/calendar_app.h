#pragma once

#include <stdbool.h>
#include <stdint.h>

void calendar_app_open(void);
void calendar_app_draw(uint32_t window_x, uint32_t window_y);
bool calendar_app_handle_key(char key);
