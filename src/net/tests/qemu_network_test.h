#pragma once

#include <stdbool.h>

bool qemu_network_test_requested(void);
void qemu_network_test_thread(void *argument);