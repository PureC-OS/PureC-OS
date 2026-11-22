#include <arch/arch.h>
#include <arch/x86_64/pat.h>
#include <kernel/diagnostics/klog.h>

#define MSR_PAT 0x277
#define PAT_TYPE_WC 0x01ULL

static bool pat_ready;
static uint64_t pat_value;
static uint64_t wc_bits;

static uint64_t index_bits(uint32_t index)
{
    return ((index & 1) ? ARCH_PTE_PWT : 0) | ((index & 2) ? ARCH_PTE_PCD : 0) |
           ((index & 4) ? ARCH_PTE_PAT : 0);
}

void arch_pat_init_bsp(void)
{
    uint32_t edx = 0;
    arch_cpuid(1, 0, NULL, NULL, NULL, &edx);
    if (!(edx & (1U << 16))) {
        klog(KLOG_WARN, "pat: not supported, framebuffer stays uncached");
        return;
    }
    uint64_t before = arch_rdmsr(MSR_PAT);
    uint64_t value = before;
    int32_t index = -1;
    for (uint32_t i = 0; i < 8; i++)
        if (((value >> (i * 8)) & 0x7) == PAT_TYPE_WC) {
            index = (int32_t)i;
            break;
        }
    if (index < 0) {
        index = 7;
        value = (value & ~(0xFFULL << 56)) | (PAT_TYPE_WC << 56);
        uint64_t flags = arch_irq_save();
        __asm__ volatile("wbinvd" ::: "memory");
        arch_wrmsr(MSR_PAT, value);
        __asm__ volatile("wbinvd" ::: "memory");
        arch_tlb_flush_all();
        arch_irq_restore(flags);
    }
    pat_value = value;
    wc_bits = index_bits((uint32_t)index);
    pat_ready = true;
    klogf(KLOG_INFO, "pat: %016llx -> %016llx, write-combining is entry %d",
          (unsigned long long)before, (unsigned long long)value, index);
}

void arch_pat_init_ap(void)
{
    if (!pat_ready || arch_rdmsr(MSR_PAT) == pat_value)
        return;
    __asm__ volatile("wbinvd" ::: "memory");
    arch_wrmsr(MSR_PAT, pat_value);
    __asm__ volatile("wbinvd" ::: "memory");
    arch_tlb_flush_all();
}

uint64_t arch_pat_wc_bits(void)
{
    return pat_ready ? wc_bits : 0;
}
