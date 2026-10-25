#pragma once
#include <stdbool.h>
#include <stdint.h>

void mmio_configure(uint64_t hhdm_offset,
                    uint64_t kernel_physical_base,
                    uint64_t kernel_virtual_base);
void mmio_init(void);
bool mmio_is_ready(void);
volatile void *mmio_map(uint64_t physical_address, uint64_t size);
