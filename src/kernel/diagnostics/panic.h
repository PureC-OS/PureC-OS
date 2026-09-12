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

/*
 * kernel_panic_manual - ручной вызов паники с захватом живых регистров.
 * Используйте KERNEL_PANIC_HERE("причина") в любом месте ядра.
 */
__attribute__((noreturn)) void kernel_panic_manual(const char *reason,
    uint64_t rip, uint64_t rsp, uint64_t rbp,
    const struct panic_registers *regs);

#define KERNEL_PANIC_HERE(reason)                                             \
    do {                                                                      \
        struct panic_registers __pr;                                          \
        uint64_t __rip = 0, __rsp = 0, __rbp = 0;                           \
        __asm__ volatile("lea 0(%%rip), %0" : "=r"(__rip) :: "memory");      \
        __asm__ volatile("mov %%rsp, %0"   : "=r"(__rsp) :: "memory");       \
        __asm__ volatile("mov %%rbp, %0"   : "=r"(__rbp) :: "memory");       \
        __asm__ volatile("mov %%rax, %0"   : "=m"(__pr.rax) :: "memory");    \
        __asm__ volatile("mov %%rbx, %0"   : "=m"(__pr.rbx) :: "memory");    \
        __asm__ volatile("mov %%rcx, %0"   : "=m"(__pr.rcx) :: "memory");    \
        __asm__ volatile("mov %%rdx, %0"   : "=m"(__pr.rdx) :: "memory");    \
        __asm__ volatile("mov %%rsi, %0"   : "=m"(__pr.rsi) :: "memory");    \
        __asm__ volatile("mov %%rdi, %0"   : "=m"(__pr.rdi) :: "memory");    \
        __asm__ volatile("mov %%r8,  %0"   : "=m"(__pr.r8)  :: "memory");    \
        __asm__ volatile("mov %%r9,  %0"   : "=m"(__pr.r9)  :: "memory");    \
        __asm__ volatile("mov %%r10, %0"   : "=m"(__pr.r10) :: "memory");    \
        __asm__ volatile("mov %%r11, %0"   : "=m"(__pr.r11) :: "memory");    \
        __asm__ volatile("mov %%r12, %0"   : "=m"(__pr.r12) :: "memory");    \
        __asm__ volatile("mov %%r13, %0"   : "=m"(__pr.r13) :: "memory");    \
        __asm__ volatile("mov %%r14, %0"   : "=m"(__pr.r14) :: "memory");    \
        __asm__ volatile("mov %%r15, %0"   : "=m"(__pr.r15) :: "memory");    \
        __pr.rbp = __rbp;                                                     \
        kernel_panic_manual((reason), __rip, __rsp, __rbp, &__pr);           \
    } while(0)
