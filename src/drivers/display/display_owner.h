#pragma once

#include <stdbool.h>
#include <stdint.h>

int32_t display_owner_claim(uint32_t pid);
void display_owner_release(uint32_t pid);
bool display_owner_active(void);
uint32_t display_owner_pid(void);
void display_owner_pump(void);
