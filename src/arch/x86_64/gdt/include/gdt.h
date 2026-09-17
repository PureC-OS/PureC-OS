#pragma once
#include <stdint.h>
#include <stdbool.h>
void gdt_init(void);
bool gdt_init_cpu(uint32_t cpu_id, uint64_t stack_top);
void gdt_set_kernel_stack(uint64_t stack_top);