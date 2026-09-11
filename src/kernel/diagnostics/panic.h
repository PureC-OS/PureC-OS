#pragma once
#include <stdint.h>

struct panic_registers {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
};

__attribute__((noreturn)) void kernel_panic(const char *reason);
__attribute__((noreturn)) void kernel_panic_exception(
    uint64_t vector,
    uint64_t error_code,
    uint64_t rip,
    uint64_t cs,
    uint64_t rflags,
    uint64_t cr2,
    const struct panic_registers *regs
);
