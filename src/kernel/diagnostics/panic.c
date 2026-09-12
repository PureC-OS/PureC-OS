#include "panic.h"
#include "boot_diag.h"
#include "klog.h"
#include "../../drivers/display/gop.h"
#include "../process/scheduler.h"
#include <stdbool.h>
#include <stddef.h>

static volatile bool panic_active = false;

/* ──────────────────────────── helpers ──────────────────────────── */

static const char *exception_name(uint64_t vec) {
    static const char *const names[32] = {
        "Divide Error",             "Debug",
        "NMI",                      "Breakpoint",
        "Overflow",                 "Bound Range Exceeded",
        "Invalid Opcode",           "Device Not Available",
        "Double Fault",             "Coprocessor Segment Overrun",
        "Invalid TSS",              "Segment Not Present",
        "Stack-Segment Fault",      "General Protection Fault",
        "Page Fault",               "Reserved",
        "x87 FP Exception",         "Alignment Check",
        "Machine Check",            "SIMD FP Exception",
        "Virtualization Exception", "Control Protection Exception",
        "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
        "Hypervisor Injection",     "VMM Communication Exception",
        "Security Exception",       "Reserved",
    };
    return vec < 32 ? names[vec] : "Unknown Interrupt";
}

static __attribute__((noreturn)) void panic_halt(void) {
    for (;;) __asm__ volatile("cli; hlt");
}

/* ──────────────────────────── banner ──────────────────────────── */

static void panic_header(const char *title) {
    __asm__ volatile("cli");
    if (panic_active) panic_halt();
    panic_active = true;
    gop_cancel_compose();
    klog_set_screen_enabled(true);
    klog_clear();

    klog(KLOG_ERROR, "");
    klog(KLOG_ERROR, "---------- Kernel Panic ----------");
    klog(KLOG_ERROR, "PureC OS   kernel BUG at          ");
    klogf(KLOG_ERROR, "  %s", title ? title : "unknown fatal error");
    klog(KLOG_ERROR, "----------------------------------");
    klogf(KLOG_ERROR, "Boot stage: %02u  %s",
          (unsigned int)boot_diag_current_stage(),
          boot_diag_current_detail());
}

/* ──────────────────────────── current thread ──────────────────── */

static void panic_thread_info(void) {
    struct thread *t = scheduler_current_thread();
    if (!t) {
        klog(KLOG_ERROR, "CPU:  <none>  (scheduler not running)");
        return;
    }
    klogf(KLOG_ERROR, "CPU:  tid=%-4u  name=%-16s  state=%u",
          t->id, t->name[0] ? t->name : "?", (unsigned int)t->state);
    klogf(KLOG_ERROR, "      rsp=0x%016llx  entry=%p",
          (unsigned long long)t->rsp, t->entry);
}

/* ──────────────────────────── control registers ───────────────── */

static void panic_ctrl_regs(void) {
    uint64_t cr0, cr2, cr3, cr4;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    klogf(KLOG_ERROR, "CR0: %016llx  CR2: %016llx",
          (unsigned long long)cr0, (unsigned long long)cr2);
    klogf(KLOG_ERROR, "CR3: %016llx  CR4: %016llx",
          (unsigned long long)cr3, (unsigned long long)cr4);
}

/* ──────────────────────────── GP registers (2-column) ─────────── */

static void panic_gp_regs(const struct panic_registers *r,
                           uint64_t rip, uint64_t rsp, uint64_t rflags) {
    klog(KLOG_ERROR, "");
    klogf(KLOG_ERROR, "RIP: %016llx  RSP: %016llx",
          (unsigned long long)rip, (unsigned long long)rsp);
    klogf(KLOG_ERROR, "RFLAGS: %016llx",
          (unsigned long long)rflags);
    klog(KLOG_ERROR, "");
    klogf(KLOG_ERROR, "RAX: %016llx  RBX: %016llx",
          (unsigned long long)r->rax, (unsigned long long)r->rbx);
    klogf(KLOG_ERROR, "RCX: %016llx  RDX: %016llx",
          (unsigned long long)r->rcx, (unsigned long long)r->rdx);
    klogf(KLOG_ERROR, "RSI: %016llx  RDI: %016llx",
          (unsigned long long)r->rsi, (unsigned long long)r->rdi);
    klogf(KLOG_ERROR, "RBP: %016llx",
          (unsigned long long)r->rbp);
    klogf(KLOG_ERROR, "R8:  %016llx  R9:  %016llx",
          (unsigned long long)r->r8,  (unsigned long long)r->r9);
    klogf(KLOG_ERROR, "R10: %016llx  R11: %016llx",
          (unsigned long long)r->r10, (unsigned long long)r->r11);
    klogf(KLOG_ERROR, "R12: %016llx  R13: %016llx",
          (unsigned long long)r->r12, (unsigned long long)r->r13);
    klogf(KLOG_ERROR, "R14: %016llx  R15: %016llx",
          (unsigned long long)r->r14, (unsigned long long)r->r15);
}

/* ──────────────────────────── call trace (RBP chain) ──────────── */

/*
 * Идём по цепочке фреймов: [rbp+0] = saved rbp, [rbp+8] = return address.
 * Точно как Linux: печатаем только адреса в kernel-space (>= 0xffffffff80000000).
 */
static void panic_call_trace(uint64_t rbp) {
    klog(KLOG_ERROR, "");
    klog(KLOG_ERROR, "Call Trace:");

    int depth = 0;
    const int MAX_DEPTH = 20;

    while (depth < MAX_DEPTH && rbp >= 0xffff800000000000ULL) {
        /* безопасное чтение — если rbp кривой, дальше не ходим */
        volatile uint64_t *frame = (volatile uint64_t *)(uintptr_t)rbp;
        uint64_t ret_addr = frame[1];   /* [rbp+8]  = return address */
        uint64_t next_rbp = frame[0];   /* [rbp+0]  = caller's rbp   */

        /* печатаем только адреса в kernel text */
        if (ret_addr >= 0xffffffff80000000ULL)
            klogf(KLOG_ERROR, "  [<%016llx>]", (unsigned long long)ret_addr);

        if (next_rbp == 0 || next_rbp <= rbp) break;
        rbp = next_rbp;
        depth++;
    }

    if (depth == 0)
        klog(KLOG_ERROR, "  (no frame pointers — compiled without -fno-omit-frame-pointer?)");
}

/* ──────────────────────────── footer ──────────────────────────── */

static void panic_footer(void) {
    klog(KLOG_ERROR, "");
    klog(KLOG_ERROR, "---  CPU halted  ---");
    klog(KLOG_ERROR, "Photograph this screen or save serial log for debugging.");
}

/* ════════════════════════════ PUBLIC API ═══════════════════════════ */

/*
 * kernel_panic — вызывается когда регистры CPU недоступны
 * (например, из обычного кода ядра, не из обработчика прерывания).
 */
void kernel_panic(const char *reason) {
    panic_header(reason);
    panic_ctrl_regs();
    panic_thread_info();

    /* захватываем текущее состояние прямо здесь */
    uint64_t rbp;
    __asm__ volatile("mov %%rbp, %0" : "=r"(rbp));
    panic_call_trace(rbp);

    panic_footer();
    panic_halt();
}

/*
 * kernel_panic_exception — вызывается из обработчика IDT (isr_handler).
 * Получает полный снимок CPU из фрейма прерывания.
 */
void kernel_panic_exception(uint64_t vector,
                            uint64_t error_code,
                            uint64_t rip,
                            uint64_t cs,
                            uint64_t rflags,
                            uint64_t cr2,
                            const struct panic_registers *regs) {
    /* Строим заголовок */
    char title[64];
    const char *name = exception_name(vector);
    /* простой sprintf вручную */
    {
        char *p = title;
        const char *prefix = "exception: #";
        while (*prefix) *p++ = *prefix++;
        /* vector as decimal */
        if (vector >= 10) { *p++ = (char)('0' + vector / 10); }
        *p++ = (char)('0' + vector % 10);
        *p++ = ' ';
        while (*name) *p++ = *name++;
        *p = '\0';
    }

    panic_header(title);

    klogf(KLOG_ERROR, "error_code: 0x%016llx  CS: 0x%04llx",
          (unsigned long long)error_code, (unsigned long long)cs);

    if (vector == 14) {  /* Page Fault */
        klogf(KLOG_ERROR,
              "PF addr: 0x%016llx  [%s|%s|%s%s%s]",
              (unsigned long long)cr2,
              (error_code & 4) ? "user" : "kernel",
              (error_code & 2) ? "write" : "read",
              (error_code & 1) ? "present" : "not-present",
              (error_code & 8) ? "|reserved-bit" : "",
              (error_code & 16) ? "|ifetch" : "");
    }

    klog(KLOG_ERROR, "");
    panic_ctrl_regs();
    panic_thread_info();

    if (regs)
        panic_gp_regs(regs, rip, 0 /* rsp from thread */, rflags);

    panic_call_trace(regs ? regs->rbp : 0);
    panic_footer();
    panic_halt();
}

/*
 * kernel_panic_manual — используется через макрос KERNEL_PANIC_HERE().
 * Все регистры уже захвачены в точке вызова.
 */
void kernel_panic_manual(const char *reason,
                          uint64_t rip, uint64_t rsp, uint64_t rbp,
                          const struct panic_registers *regs) {
    panic_header(reason);
    klog(KLOG_ERROR, "[manual panic — triggered by kernel code]");
    klog(KLOG_ERROR, "");
    panic_ctrl_regs();
    panic_thread_info();
    if (regs)
        panic_gp_regs(regs, rip, rsp, 0);
    panic_call_trace(rbp);
    panic_footer();
    panic_halt();
}
