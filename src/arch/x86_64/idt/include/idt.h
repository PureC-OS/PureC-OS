#pragma once
#include <stdint.h>

void idt_init(void);
void idt_init_cpu(void);
void idt_set_gate(int n, uint64_t handler, uint8_t flags);
void idt_set_irq_handler(uint8_t irq, void *fn, void *ctx);
void idt_clear_irq_handler(uint8_t irq);
void idt_set_vector_handler(uint8_t vector, void (*fn)(void *), void *ctx);
void idt_unmask_irq(uint8_t irq);
void idt_mask_irq(uint8_t irq);
uint64_t idt_vector_count(uint8_t vector);
void idt_check_interrupt_rates(void);
