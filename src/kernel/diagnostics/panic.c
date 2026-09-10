#include "panic.h"
#include "boot_diag.h"
#include "klog.h"
#include "../../drivers/display/gop.h"
#include "../process/scheduler.h"
#include <stdbool.h>
#include <stddef.h>

static volatile bool panic_active = false;

static const char *exception_name(uint64_t vector){
    static const char *const names[32] = {
        "divide error", "debug", "non-maskable interrupt", "breakpoint",
        "overflow", "bound range exceeded", "invalid opcode", "device not available",
        "double fault", "coprocessor segment overrun", "invalid TSS", "segment not present",
        "stack-segment fault", "general protection fault", "page fault", "reserved",
        "x87 floating-point exception", "alignment check", "machine check", "SIMD floating-point exception",
        "virtualization exception", "control protection exception", "reserved", "reserved",
        "reserved", "reserved", "reserved", "reserved",
        "hypervisor injection exception", "VMM communication exception", "security exception", "reserved"
    };
    return vector < 32 ? names[vector] : "unexpected interrupt";
}

static __attribute__((noreturn)) void panic_halt(void){
    for(;;) __asm__ volatile("cli; hlt");
}

static void panic_begin(void){
    __asm__ volatile("cli");
    if(panic_active) panic_halt();
    panic_active = true;
    gop_cancel_compose();
    klog_set_screen_enabled(true);
    klog_clear();
    klog(KLOG_ERROR, "============================================================");
    klog(KLOG_ERROR, "KERNEL PANIC - PureC OS cannot continue");
    klog(KLOG_ERROR, "============================================================");
    klogf(KLOG_ERROR, "Boot stage: %02u (%s)",
          (unsigned int)boot_diag_current_stage(), boot_diag_current_detail());
}

static void panic_current_thread(void){
    struct thread *t = scheduler_current_thread();
    if(!t){
        klog(KLOG_ERROR, "thread: <none> (scheduler not running)");
        return;
    }
    uint64_t rsp = t->rsp;
    klogf(KLOG_ERROR, "thread: id=%u name=%s rsp=0x%016llx entry=%p state=%u",
          t->id, t->name[0] ? t->name : "?", rsp, t->entry,
          (unsigned int)t->state);
}

static void panic_backtrace(void){
    uint64_t rsp;
    __asm__ volatile("mov %%rsp, %0" : "=r"(rsp));
    klogf(KLOG_ERROR, "rsp=0x%016llx backtrace:", rsp);

    for(int i=0;i<16;i++){
        uint64_t addr = rsp + (uint64_t)i*8ULL;
        uint64_t val = 0;
        if(addr < 0xffff800000000000ULL) break;
        __asm__ volatile("" ::: "memory");
        val = *(volatile uint64_t*)(uintptr_t)addr;
        const char *mark = (val >= 0xffffffff80000000ULL) ? " <kernel-text?>"
                         : (val == 0x8000ULL) ? " <== 0x8000 poison?"
                         : "";
        klogf(KLOG_ERROR, "  [%d] 0x%016llx: 0x%016llx%s", i, addr, val, mark);
    }
}
static void panic_control_registers(void){
    uint64_t cr0, cr3, cr4;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    klogf(KLOG_ERROR, "cr0=%016llx cr3=%016llx cr4=%016llx", cr0, cr3, cr4);
}

void kernel_panic(const char *reason){
    panic_begin();
    klogf(KLOG_ERROR, "Reason: %s", reason ? reason : "unknown fatal error");
    panic_current_thread();
    panic_control_registers();
    panic_backtrace();
    klog(KLOG_ERROR, "CPU halted. Photograph this screen and report the last BOOT stage.");
    panic_halt();
}

void kernel_panic_exception(uint64_t vector,
                            uint64_t error_code,
                            uint64_t rip,
                            uint64_t cs,
                            uint64_t rflags,
                            uint64_t cr2,
                            const struct panic_registers *regs){
    panic_begin();
    klogf(KLOG_ERROR, "Exception: #%llu %s", vector, exception_name(vector));
    klogf(KLOG_ERROR, "error=0x%016llx rip=0x%016llx cs=0x%04llx rflags=0x%016llx",
          error_code, rip, cs, rflags);
    if(vector == 14){
        klogf(KLOG_ERROR, "cr2=0x%016llx access=%s mode=%s present=%s reserved=%s instruction=%s",
              cr2,
              (error_code & 2) ? "write" : "read",
              (error_code & 4) ? "user" : "kernel",
              (error_code & 1) ? "yes" : "no",
              (error_code & 8) ? "yes" : "no",
              (error_code & 16) ? "yes" : "no");
    }
    panic_control_registers();
    panic_current_thread();
    if(regs){
        klogf(KLOG_ERROR, "rax=%016llx rbx=%016llx rcx=%016llx rdx=%016llx",
              regs->rax, regs->rbx, regs->rcx, regs->rdx);
        klogf(KLOG_ERROR, "rsi=%016llx rdi=%016llx rbp=%016llx",
              regs->rsi, regs->rdi, regs->rbp);
        klogf(KLOG_ERROR, "r8 =%016llx r9 =%016llx r10=%016llx r11=%016llx",
              regs->r8, regs->r9, regs->r10, regs->r11);
        klogf(KLOG_ERROR, "r12=%016llx r13=%016llx r14=%016llx r15=%016llx",
              regs->r12, regs->r13, regs->r14, regs->r15);
    }
    panic_backtrace();
    klog(KLOG_ERROR, "CPU halted. Photograph this panic screen for debugging.");
    panic_halt();
}
