#pragma once
#include <stdint.h>

void gdt_init(void);
void gdt_set_kernel_stack(uint64_t stack_top);
