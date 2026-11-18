#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ARCH_NAME "x86_64"
#define ARCH_IRQ_FLAG (1ULL << 9)

#define ARCH_MSR_EFER 0xC0000080U
#define ARCH_MSR_FS_BASE 0xC0000100U
#define ARCH_MSR_GS_BASE 0xC0000101U
#define ARCH_MSR_KERNEL_GS_BASE 0xC0000102U

static inline void arch_cpu_relax(void)
{
    __asm__ volatile("pause" ::: "memory");
}

static inline void arch_cpu_halt(void)
{
    __asm__ volatile("hlt");
}

static inline void arch_cpu_idle(void)
{
    __asm__ volatile("sti; hlt" ::: "memory");
}

static inline __attribute__((noreturn)) void arch_cpu_stop(void)
{
    for (;;)
        __asm__ volatile("cli; hlt");
}

static inline void arch_irq_enable(void)
{
    __asm__ volatile("sti" ::: "memory");
}

static inline void arch_irq_disable(void)
{
    __asm__ volatile("cli" ::: "memory");
}

static inline uint64_t arch_irq_save(void)
{
#ifdef PUREC_HOST_TEST
    return 0;
#else
    uint64_t flags;
    __asm__ volatile("pushfq; pop %0; cli" : "=r"(flags)::"memory");
    return flags;
#endif
}

static inline void arch_irq_restore(uint64_t flags)
{
#ifdef PUREC_HOST_TEST
    (void)flags;
#else
    if (flags & ARCH_IRQ_FLAG)
        __asm__ volatile("sti" ::: "memory");
#endif
}

static inline bool arch_irq_enabled(void)
{
    uint64_t flags;
    __asm__ volatile("pushfq; pop %0" : "=r"(flags)::"memory");
    return (flags & ARCH_IRQ_FLAG) != 0;
}

static inline void arch_mb(void)
{
    __asm__ volatile("mfence" ::: "memory");
}

static inline void arch_rmb(void)
{
    __asm__ volatile("lfence" ::: "memory");
}

static inline void arch_wmb(void)
{
    __asm__ volatile("sfence" ::: "memory");
}

static inline void arch_compiler_barrier(void)
{
    __asm__ volatile("" ::: "memory");
}

static inline uint64_t arch_read_timestamp(void)
{
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static inline void arch_cpuid(uint32_t leaf, uint32_t subleaf, uint32_t *a,
                              uint32_t *b, uint32_t *c, uint32_t *d)
{
    uint32_t ra, rb, rc, rd;
    __asm__ volatile("cpuid"
                     : "=a"(ra), "=b"(rb), "=c"(rc), "=d"(rd)
                     : "a"(leaf), "c"(subleaf));
    if (a)
        *a = ra;
    if (b)
        *b = rb;
    if (c)
        *c = rc;
    if (d)
        *d = rd;
}

static inline uint64_t arch_rdmsr(uint32_t msr)
{
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static inline void arch_wrmsr(uint32_t msr, uint64_t value)
{
    __asm__ volatile("wrmsr"
                     :
                     : "c"(msr), "a"((uint32_t)value),
                       "d"((uint32_t)(value >> 32))
                     : "memory");
}

static inline void arch_tlb_flush_page(uint64_t va)
{
    __asm__ volatile("invlpg (%0)" ::"r"(va) : "memory");
}

static inline uint64_t arch_read_cr3(void)
{
    uint64_t v;
    __asm__ volatile("mov %%cr3, %0" : "=r"(v));
    return v;
}

static inline void arch_write_cr3(uint64_t v)
{
    __asm__ volatile("mov %0, %%cr3" ::"r"(v) : "memory");
}

static inline void arch_tlb_flush_all(void)
{
    arch_write_cr3(arch_read_cr3());
}

static inline uint64_t arch_read_cr2(void)
{
    uint64_t v;
    __asm__ volatile("mov %%cr2, %0" : "=r"(v));
    return v;
}

static inline uint64_t arch_read_cr0(void)
{
    uint64_t v;
    __asm__ volatile("mov %%cr0, %0" : "=r"(v));
    return v;
}

static inline uint64_t arch_read_cr4(void)
{
    uint64_t v;
    __asm__ volatile("mov %%cr4, %0" : "=r"(v));
    return v;
}

static inline void arch_write_cr0(uint64_t v)
{
    __asm__ volatile("mov %0, %%cr0" ::"r"(v) : "memory");
}

static inline void arch_write_cr4(uint64_t v)
{
    __asm__ volatile("mov %0, %%cr4" ::"r"(v) : "memory");
}

static inline void arch_tlb_flush_local_user(void)
{
    uint64_t cr3;
    __asm__ volatile("mov %%cr3,%0; mov %0,%%cr3" : "=r"(cr3)::"memory");
}

static inline void arch_tlb_flush_local_global(void)
{
    uint64_t cr4, cr3;
    __asm__ volatile("mov %%cr4,%0; mov %%cr3,%1" : "=r"(cr4), "=r"(cr3));
    if (cr4 & (1ULL << 7))
        __asm__ volatile("mov %0,%%cr4; mov %1,%%cr4" ::"r"(cr4 & ~(1ULL << 7)),
                         "r"(cr4)
                         : "memory");
    else
        __asm__ volatile("mov %0,%%cr3" ::"r"(cr3) : "memory");
}

static inline void arch_percpu_set_base(uint64_t base)
{
    arch_wrmsr(ARCH_MSR_KERNEL_GS_BASE, 0);
    arch_wrmsr(ARCH_MSR_GS_BASE, base);
}

#define ARCH_PERCPU_LOAD32(off)                                              \
    ({                                                                       \
        uint32_t __v;                                                        \
        __asm__ volatile("movl %%gs:%c1, %0" : "=r"(__v) : "i"(off));        \
        __v;                                                                 \
    })

#define ARCH_PERCPU_LOAD64(off)                                              \
    ({                                                                       \
        uint64_t __v;                                                        \
        __asm__ volatile("movq %%gs:%c1, %0" : "=r"(__v) : "i"(off));        \
        __v;                                                                 \
    })

static inline __attribute__((always_inline)) uint64_t arch_get_ip(void)
{
    uint64_t v;
    __asm__ volatile("lea 0(%%rip), %0" : "=r"(v)::"memory");
    return v;
}

static inline __attribute__((always_inline)) uint64_t arch_get_sp(void)
{
    uint64_t v;
    __asm__ volatile("mov %%rsp, %0" : "=r"(v)::"memory");
    return v;
}

static inline __attribute__((always_inline)) uint64_t arch_get_fp(void)
{
    uint64_t v;
    __asm__ volatile("mov %%rbp, %0" : "=r"(v)::"memory");
    return v;
}

#define ARCH_CAPTURE_PANIC_REGS(pr)                                          \
    __asm__ volatile("mov %%rax, %0" : "=m"((pr).rax)::"memory");       \
    __asm__ volatile("mov %%rbx, %0" : "=m"((pr).rbx)::"memory");       \
    __asm__ volatile("mov %%rcx, %0" : "=m"((pr).rcx)::"memory");       \
    __asm__ volatile("mov %%rdx, %0" : "=m"((pr).rdx)::"memory");       \
    __asm__ volatile("mov %%rsi, %0" : "=m"((pr).rsi)::"memory");       \
    __asm__ volatile("mov %%rdi, %0" : "=m"((pr).rdi)::"memory");       \
    __asm__ volatile("mov %%r8, %0" : "=m"((pr).r8)::"memory");       \
    __asm__ volatile("mov %%r9, %0" : "=m"((pr).r9)::"memory");       \
    __asm__ volatile("mov %%r10, %0" : "=m"((pr).r10)::"memory");       \
    __asm__ volatile("mov %%r11, %0" : "=m"((pr).r11)::"memory");       \
    __asm__ volatile("mov %%r12, %0" : "=m"((pr).r12)::"memory");       \
    __asm__ volatile("mov %%r13, %0" : "=m"((pr).r13)::"memory");       \
    __asm__ volatile("mov %%r14, %0" : "=m"((pr).r14)::"memory");       \
    __asm__ volatile("mov %%r15, %0" : "=m"((pr).r15)::"memory");

static inline void arch_set_tls_base(uint64_t base)
{
#ifndef PUREC_HOST_TEST
    arch_wrmsr(ARCH_MSR_FS_BASE, base);
#else
    (void)base;
#endif
}

static inline uint64_t arch_fault_address(void)
{
    return arch_read_cr2();
}

static inline void arch_breakpoint(void)
{
    __asm__ volatile("int3");
}

static inline int64_t arch_kernel_syscall(uint64_t n, uint64_t a1, uint64_t a2,
                                          uint64_t a3, uint64_t a4, uint64_t a5)
{
    int64_t ret;
    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(n), "b"(a1), "c"(a2), "d"(a3), "S"(a4), "D"(a5)
                     : "r10", "r8", "memory");
    return ret;
}

static inline bool arch_hw_random_available(void)
{
    uint32_t ecx;
    arch_cpuid(1, 0, NULL, NULL, &ecx, NULL);
    return (ecx & (1U << 30)) != 0;
}

static inline bool arch_hw_random_u64(uint64_t *value)
{
    uint64_t result;
    unsigned char ok;
    __asm__ volatile("rdrand %0; setc %1" : "=r"(result), "=qm"(ok));
    if (!ok)
        return false;
    *value = result;
    return true;
}
