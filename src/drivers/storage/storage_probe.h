#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "storage_types.h"

void storage_probe_init(void);
uint32_t storage_controller_count(void);
int32_t storage_controller_list(struct storage_controller_info *controllers, uint32_t capacity);