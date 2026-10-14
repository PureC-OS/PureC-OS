#include "panic.h"
#include "../../drivers/display/gop.h"
#include "../process/scheduler.h"
#include "../smp/smp.h"
#include "boot_diag.h"
#include "klog.h"
#include "ksym.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static volatile bool panic_active = false;

static const char *exception_name(uint64_t vec) {
  static const char *const names[32] = {
      "Divide Error",
      "Debug",
      "NMI",
      "Breakpoint",
      "Overflow",
      "Bound Range Exceeded",
      "Invalid Opcode",
      "Device Not Available",
      "Double Fault",
      "Coprocessor Segment Overrun",
      "Invalid TSS",
      "Segment Not Present",
      "Stack-Segment Fault",
      "General Protection Fault",
      "Page Fault",
      "Reserved",
      "x87 FP Exception",
      "Alignment Check",
      "Machine Check",
      "SIMD FP Exception",
      "Virtualization Exception",
      "Control Protection Exception",
      "Reserved",
      "Reserved",
      "Reserved",
      "Reserved",
      "Reserved",
      "Reserved",
      "Hypervisor Injection",
      "VMM Communication Exception",
      "Security Exception",
      "Reserved",
  };
  return vec < 32 ? names[vec] : "Unknown Interrupt";
}

static __attribute__((noreturn)) void panic_halt(void) {
  gop_present_forced();
  for (;;)
    __asm__ volatile("cli; hlt");
}

static void panic_header(const char *title) {
  __asm__ volatile("cli");
  if (panic_active)
    panic_halt();
  panic_active = true;
  smp_stop_others();
  klog_panic_mode();
  gop_cancel_compose();
  klog_set_screen_enabled(true);
  klog_clear();

  klog(KLOG_ERROR, "");
  klog(KLOG_ERROR,
       "========================Kernel Panic=======================");
  klog(KLOG_ERROR, "PureC OS   kernel BUG at          ");
  klogf(KLOG_ERROR, "  %s", title ? title : "unknown fatal error");
  klog(KLOG_ERROR,
       "===========================================================");
  klogf(KLOG_ERROR, "Boot stage: %02u  %s",
        (unsigned int)boot_diag_current_stage(), boot_diag_current_detail());
  if (ksym_ready())
    klog(KLOG_ERROR, "ksyms: ready (kernel.map embedded in .ksyms)");
  else
    klog(KLOG_ERROR, "ksyms: MISSING — backtrace will show raw addresses");
}

static void panic_thread_info(void) {
  struct thread *t = scheduler_current_thread();
  if (!t) {
    klog(KLOG_ERROR, "CPU:  <none>  (scheduler not running)");
    return;
  }
  klogf(KLOG_ERROR, "CPU:  tid=%u  state=%u", (unsigned int)t->id,
        (unsigned int)t->state);
  klogf(KLOG_ERROR, "      rsp=0x%016llx  entry=%p", (unsigned long long)t->rsp,
        t->entry);
}

static void panic_ctrl_regs(void) {
  uint64_t cr0, cr2, cr3, cr4;
  __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
  __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
  __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
  __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
  klogf(KLOG_ERROR, "CR0: %016llx  CR2: %016llx", (unsigned long long)cr0,
        (unsigned long long)cr2);
  klogf(KLOG_ERROR, "CR3: %016llx  CR4: %016llx", (unsigned long long)cr3,
        (unsigned long long)cr4);
}

static void __attribute__((unused)) panic_addr_sym(uint64_t addr) {
  const char *name = 0;
  uint64_t off = 0;
  if (ksym_resolve(addr, &name, &off))
    klogf(KLOG_ERROR, "  [<%016llx>] %s+0x%llx", (unsigned long long)addr,
          name, (unsigned long long)off);
  else
    klogf(KLOG_ERROR, "  [<%016llx>] ??", (unsigned long long)addr);
}

static void panic_rip_sym(uint64_t rip) {
  const char *name = 0;
  uint64_t off = 0;
  if (ksym_resolve(rip, &name, &off))
    klogf(KLOG_ERROR, "RIP sym: %s+0x%llx", name, (unsigned long long)off);
  else
    klog(KLOG_ERROR, "RIP sym: ?? (address not in kernel.map)");
}

static void panic_gp_regs(const struct panic_registers *r, uint64_t rip,
                          uint64_t rsp, uint64_t rflags) {
  klog(KLOG_ERROR, "");
  klogf(KLOG_ERROR, "RIP: %016llx  RSP: %016llx", (unsigned long long)rip,
        (unsigned long long)rsp);
  panic_rip_sym(rip);
  klogf(KLOG_ERROR, "RFLAGS: %016llx", (unsigned long long)rflags);
  klog(KLOG_ERROR, "");
  klogf(KLOG_ERROR, "RAX: %016llx  RBX: %016llx", (unsigned long long)r->rax,
        (unsigned long long)r->rbx);
  klogf(KLOG_ERROR, "RCX: %016llx  RDX: %016llx", (unsigned long long)r->rcx,
        (unsigned long long)r->rdx);
  klogf(KLOG_ERROR, "RSI: %016llx  RDI: %016llx", (unsigned long long)r->rsi,
        (unsigned long long)r->rdi);
  klogf(KLOG_ERROR, "RBP: %016llx", (unsigned long long)r->rbp);
  klogf(KLOG_ERROR, "R8:  %016llx  R9:  %016llx", (unsigned long long)r->r8,
        (unsigned long long)r->r9);
  klogf(KLOG_ERROR, "R10: %016llx  R11: %016llx", (unsigned long long)r->r10,
        (unsigned long long)r->r11);
  klogf(KLOG_ERROR, "R12: %016llx  R13: %016llx", (unsigned long long)r->r12,
        (unsigned long long)r->r13);
  klogf(KLOG_ERROR, "R14: %016llx  R15: %016llx", (unsigned long long)r->r14,
        (unsigned long long)r->r15);
}

static bool panic_is_kernel_addr(uint64_t a) {
  return a >= 0xffff800000000000ULL;
}

static void panic_stack_dump(uint64_t rsp) {
  klog(KLOG_ERROR, "");
  klog(KLOG_ERROR, "Stack dump (32 qwords from RSP):");
  if (!panic_is_kernel_addr(rsp)) {
    klog(KLOG_ERROR, "  (RSP not in kernel half — skip)");
    return;
  }
  volatile uint64_t *sp = (volatile uint64_t *)(uintptr_t)rsp;
  for (int i = 0; i < 32; i += 4) {
    klogf(KLOG_ERROR, "  rsp+%u: %016llx %016llx %016llx %016llx",
          (unsigned int)(i * 8), (unsigned long long)sp[i],
          (unsigned long long)sp[i + 1], (unsigned long long)sp[i + 2],
          (unsigned long long)sp[i + 3]);
  }
}

static void panic_call_trace(uint64_t rbp, uint64_t rsp) {
  klog(KLOG_ERROR, "");
  klog(KLOG_ERROR, "Call Trace (kernel.map via .ksyms):");

  int depth = 0;
  const int max_depth = 24;
  int printed = 0;

  while (depth < max_depth && panic_is_kernel_addr(rbp)) {
    volatile uint64_t *frame = (volatile uint64_t *)(uintptr_t)rbp;
    uint64_t next_rbp = frame[0];
    uint64_t ret_addr = frame[1];

    if (ret_addr >= 0xffffffff80000000ULL) {
      const char *name = 0;
      uint64_t off = 0;
      if (ksym_resolve(ret_addr, &name, &off))
        klogf(KLOG_ERROR, "  #%u [<%016llx>] %s+0x%llx", (unsigned int)printed,
              (unsigned long long)ret_addr, name, (unsigned long long)off);
      else
        klogf(KLOG_ERROR, "  #%u [<%016llx>] ??", (unsigned int)printed,
              (unsigned long long)ret_addr);
      printed++;
    }

    if (next_rbp == 0 || next_rbp <= rbp)
      break;
    if (!panic_is_kernel_addr(next_rbp))
      break;
    rbp = next_rbp;
    depth++;
  }

  if (printed == 0) {
    klog(KLOG_ERROR, "  (empty — no kernel return addresses found)");
    klog(KLOG_ERROR, "  (no frame pointers — check -fno-omit-frame-pointer?)");
  }

  panic_stack_dump(rsp);
}

static void panic_footer(void) {
  klog(KLOG_ERROR, "");
  klog(KLOG_ERROR, "========================CPU HALTED=======================");
  klog(KLOG_ERROR, "Attach kernel.map line for RIP + top of Call Trace.");
  klog(KLOG_ERROR, "https://github.com/PureC-OS/PureC-OS/issues/new");
}

void kernel_panic(const char *reason) {
  panic_header(reason);
  panic_ctrl_regs();
  panic_thread_info();
  uint64_t rbp, rsp;
  __asm__ volatile("mov %%rbp, %0" : "=r"(rbp));
  __asm__ volatile("mov %%rsp, %0" : "=r"(rsp));
  panic_call_trace(rbp, rsp);

  panic_footer();
  panic_halt();
}

void kernel_panic_exception(uint64_t vector, uint64_t error_code, uint64_t rip,
                            uint64_t cs, uint64_t rflags, uint64_t cr2,
                            const struct panic_registers *regs) {
  char title[64];
  const char *name = exception_name(vector);
  {
    char *p = title;
    const char *prefix = "exception: #";
    while (*prefix)
      *p++ = *prefix++;
    if (vector >= 10) {
      *p++ = (char)('0' + vector / 10);
    }
    *p++ = (char)('0' + vector % 10);
    *p++ = ' ';
    while (*name)
      *p++ = *name++;
    *p = '\0';
  }

  panic_header(title);

  klogf(KLOG_ERROR, "error_code: 0x%016llx  CS: 0x%04llx",
        (unsigned long long)error_code, (unsigned long long)cs);

  if (vector == 14) {
    klogf(KLOG_ERROR, "PF addr: 0x%016llx  [%s|%s|%s%s%s]",
          (unsigned long long)cr2, (error_code & 4) ? "user" : "kernel",
          (error_code & 2) ? "write" : "read",
          (error_code & 1) ? "present" : "not-present",
          (error_code & 8) ? "|reserved-bit" : "",
          (error_code & 16) ? "|ifetch" : "");
  }

  klog(KLOG_ERROR, "");
  panic_ctrl_regs();
  panic_thread_info();

  uint64_t rsp = 0;
  uint64_t rbp = 0;
  if (regs) {
    struct thread *t = scheduler_current_thread();
    rsp = t ? t->rsp : 0;
    rbp = regs->rbp;
    panic_gp_regs(regs, rip, rsp, rflags);
  } else {
    panic_rip_sym(rip);
  }

  panic_call_trace(rbp, rsp);
  panic_footer();
  panic_halt();
}

void kernel_panic_manual(const char *reason, uint64_t rip, uint64_t rsp,
                         uint64_t rbp, const struct panic_registers *regs) {
  panic_header(reason);
  klog(KLOG_ERROR, "[manual panic — triggered by kernel code]");
  klog(KLOG_ERROR, "");
  panic_ctrl_regs();
  panic_thread_info();
  if (regs)
    panic_gp_regs(regs, rip, rsp, 0);
  else
    panic_rip_sym(rip);
  panic_call_trace(rbp, rsp);
  panic_footer();
  panic_halt();
}
