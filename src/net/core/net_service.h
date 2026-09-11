#pragma once

#include <stdbool.h>

bool net_service_init(void);
bool net_service_is_ready(void);
void net_service_thread(void *argument);
