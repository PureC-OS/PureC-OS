#pragma once
#include <stdint.h>
#include <arch/arch.h>

struct panic_registers
{
  uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
  uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
};

#ifdef PUREC_HOST_TEST
__attribute__((noreturn)) void kernel_panic(const char *reason);
#else
__attribute__((noreturn)) void kernel_panic_at(const char *file, int line,
                                               const char *func,
                                               const char *reason);
__attribute__((noreturn)) void kernel_panic_exception_at(
    const char *file, int line, const char *func, uint64_t vector,
    uint64_t error_code, uint64_t rip, uint64_t cs, uint64_t rflags,
    uint64_t cr2, const struct panic_registers *regs);
__attribute__((noreturn)) void kernel_panic_manual_at(
    const char *file, int line, const char *func, const char *reason,
    uint64_t rip, uint64_t rsp, uint64_t rbp,
    const struct panic_registers *regs);

#define kernel_panic(reason) \
  kernel_panic_at(__FILE__, __LINE__, __func__, (reason))
#define kernel_panic_exception(vector, error_code, rip, cs, rflags, cr2, regs) \
  kernel_panic_exception_at(__FILE__, __LINE__, __func__, (vector),            \
                            (error_code), (rip), (cs), (rflags), (cr2), (regs))
#endif

#define KERNEL_PANIC_HERE(reason)                                         \
  do                                                                      \
  {                                                                       \
    struct panic_registers __pr;                                          \
    uint64_t __rip = 0, __rsp = 0, __rbp = 0;                             \
    __rip = arch_get_ip();                                                \
    __rsp = arch_get_sp();                                                \
    __rbp = arch_get_fp();                                                \
    ARCH_CAPTURE_PANIC_REGS(__pr);                                        \
    __pr.rbp = __rbp;                                                     \
    kernel_panic_manual_at(__FILE__, __LINE__, __func__, (reason), __rip, \
                           __rsp, __rbp, &__pr);                          \
  } while (0)
