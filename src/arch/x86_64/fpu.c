// x86-64 FPU/SSE enable + per-thread state (see fpu.h).
// Eager switching: fxsave/fxrstor on every context switch. ~100-200
// cycles each, negligible next to everything else a switch does, and
// trivially correct (no #NM lazy dance).

#include "fpu.h"
#include "../../kernel/diagnostics/klog.h"
#include "../../kernel/diagnostics/panic.h"
#include "../../lib/string.h"

#define FPU_CR0_EM (1ULL << 2)
#define FPU_CR0_TS (1ULL << 3)
#define FPU_CR0_NE (1ULL << 5)
#define FPU_CR4_OSFXSR (1ULL << 9)
#define FPU_CR4_OSXMMEXCPT (1ULL << 10)

static uint8_t fpu_template[FPU_STATE_SIZE] __attribute__((aligned(16)));
static bool fpu_ready = false;

static void cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx,
                  uint32_t *ecx, uint32_t *edx) {
    __asm__ volatile("cpuid"
                     : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                     : "a"(leaf), "c"(0));
}

static uint64_t read_cr0(void) {
    uint64_t value;
    __asm__ volatile("mov %%cr0, %0" : "=r"(value));
    return value;
}

static void write_cr0(uint64_t value) {
    __asm__ volatile("mov %0, %%cr0" ::"r"(value) : "memory");
}

static uint64_t read_cr4(void) {
    uint64_t value;
    __asm__ volatile("mov %%cr4, %0" : "=r"(value));
    return value;
}

static void write_cr4(uint64_t value) {
    __asm__ volatile("mov %0, %%cr4" ::"r"(value) : "memory");
}

void fpu_init(void) {
    if (fpu_ready) return;
    uint32_t eax, ebx, ecx, edx;
    cpuid(1, &eax, &ebx, &ecx, &edx);
    if (!(edx & (1U << 24))) kernel_panic("CPU lacks FXSR, cannot run SSE userspace");
    if (!(edx & (1U << 25))) kernel_panic("CPU lacks SSE, cannot run SSE userspace");
    // Native x87 errors, no emulation, no task-switch traps (eager switch).
    write_cr0((read_cr0() & ~(FPU_CR0_EM | FPU_CR0_TS)) | FPU_CR0_NE);
    write_cr4(read_cr4() | FPU_CR4_OSFXSR | FPU_CR4_OSXMMEXCPT);
    // Canonical clean state: all exceptions masked, MXCSR default.
    __asm__ volatile("fninit");
    __asm__ volatile("fxsave %0" : "=m"(fpu_template) :: "memory");
    fpu_ready = true;
    klog(KLOG_OK, "fpu: FXSR/SSE enabled, per-thread eager switching");
}

bool fpu_available(void) {
    return fpu_ready;
}

void fpu_thread_init(void *area) {
    if (!area) return;
    if (!fpu_ready) {
        memset(area, 0, FPU_STATE_SIZE);
        return;
    }
    memcpy(area, fpu_template, FPU_STATE_SIZE);
}

void fpu_save(void *area) {
    if (!area || !fpu_ready) return;
    __asm__ volatile("fxsave %0" : "=m"(*(uint8_t(*)[FPU_STATE_SIZE])area) :: "memory");
}

void fpu_restore(const void *area) {
    if (!area || !fpu_ready) return;
    __asm__ volatile("fxrstor %0" ::"m"(*(const uint8_t(*)[FPU_STATE_SIZE])area) : "memory");
}
