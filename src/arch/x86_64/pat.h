#pragma once
#include <stdint.h>

void arch_pat_init_bsp(void);
void arch_pat_init_ap(void);
uint64_t arch_pat_wc_bits(void);
