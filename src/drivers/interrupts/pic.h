#pragma once
#include <stdint.h>
void pic_remap(uint8_t offset1, uint8_t offset2);
void pic_mask_all(void);
void pic_mask_irq(uint8_t irq);
void pic_unmask_irq(uint8_t irq);
