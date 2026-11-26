#pragma once

#include <stdbool.h>
#include <stdint.h>

int32_t sudo_elevate(const char *password);
bool sudo_may_manage_network(void);
