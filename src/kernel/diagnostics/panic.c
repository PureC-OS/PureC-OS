#include <arch/arch.h>
#include "panic.h"
#include "../../drivers/display/gop.h"
#include "../process/scheduler.h"
#include "../smp/smp.h"
#include "boot_diag.h"
#include "build_stamp.h"
#include "klog.h"
#include "ksym.h"
#include "panic_qr.h"
#include "panic_screen.h"
#include "../../lib/format.h"
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static volatile bool panic_active = false;

static const char *exception_name(uint64_t vec)
{
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

static __attribute__((noreturn)) void panic_halt(void)
{
  gop_present_forced();
  arch_cpu_stop();
}

static void out(enum pscr_tone tone, const char *fmt, ...)
{
  char line[192];
  va_list ap;
  va_start(ap, fmt);
  kvformat(line, sizeof(line), fmt, ap);
  va_end(ap);
  klog(KLOG_ERROR, line);
  pscr_line(tone, line);
}

static void section(const char *title)
{
  klogf(KLOG_ERROR, "-- %s --", title);
  pscr_section(title);
}

static void panic_header(int64_t kind, const char *title, const char *file,
                         int line, const char *func)
{
  arch_irq_disable();
  if (panic_active)
    panic_halt();
  panic_active = true;
  smp_stop_others();
  klog_panic_mode();
  klog_set_screen_enabled(false);
  gop_kernel_reclaim();
  gop_cancel_compose();

  const struct panic_theme *theme = panic_theme_for(kind);
  pscr_begin(theme, kind >= 0 ? theme->name : title);

  klog(KLOG_ERROR, "");
  klog(KLOG_ERROR, "========================Kernel Panic=======================");
  klogf(KLOG_ERROR, "%s %s", theme->code, theme->name);

  section("What happened");
  out(PSCR_TEXT, "%s", title ? title : "unknown fatal error");
  out(PSCR_DIM, "at %s:%d %s()", file ? file : "?", line, func ? func : "?");
  out(PSCR_DIM, "build %s  %s", kernel_build_uuid, kernel_build_time);
  out(PSCR_DIM, "boot stage %02u  %s", (unsigned int)boot_diag_current_stage(),
      boot_diag_current_detail());
  if (!ksym_ready())
    out(PSCR_ACCENT, "ksyms missing: addresses are not resolved to names");
}

static void panic_cpu_state(void)
{
  section("CPU state");
  struct thread *t = scheduler_current_thread();
  if (t)
    out(PSCR_TEXT, "thread %u  state %u  rsp %016llx", (unsigned int)t->id,
        (unsigned int)t->state, (unsigned long long)t->rsp);
  else
    out(PSCR_DIM, "no thread (scheduler not running)");
  out(PSCR_TEXT, "CR0 %016llx  CR2 %016llx", (unsigned long long)arch_read_cr0(),
      (unsigned long long)arch_read_cr2());
  out(PSCR_TEXT, "CR3 %016llx  CR4 %016llx", (unsigned long long)arch_read_cr3(),
      (unsigned long long)arch_read_cr4());
}

static void panic_rip_sym(uint64_t rip)
{
  const char *name = 0;
  uint64_t off = 0;
  if (ksym_resolve(rip, &name, &off))
    out(PSCR_ACCENT, "RIP %016llx  %s+0x%llx", (unsigned long long)rip, name,
        (unsigned long long)off);
  else
    out(PSCR_ACCENT, "RIP %016llx  ??", (unsigned long long)rip);
}

static void panic_gp_regs(const struct panic_registers *r, uint64_t rsp,
                          uint64_t rflags)
{
  section("Registers");
  out(PSCR_TEXT, "RAX %016llx  RBX %016llx  RCX %016llx", (unsigned long long)r->rax,
      (unsigned long long)r->rbx, (unsigned long long)r->rcx);
  out(PSCR_TEXT, "RDX %016llx  RSI %016llx  RDI %016llx", (unsigned long long)r->rdx,
      (unsigned long long)r->rsi, (unsigned long long)r->rdi);
  out(PSCR_TEXT, "RBP %016llx  RSP %016llx  R8  %016llx", (unsigned long long)r->rbp,
      (unsigned long long)rsp, (unsigned long long)r->r8);
  out(PSCR_TEXT, "R9  %016llx  R10 %016llx  R11 %016llx", (unsigned long long)r->r9,
      (unsigned long long)r->r10, (unsigned long long)r->r11);
  out(PSCR_TEXT, "R12 %016llx  R13 %016llx  R14 %016llx", (unsigned long long)r->r12,
      (unsigned long long)r->r13, (unsigned long long)r->r14);
  out(PSCR_TEXT, "R15 %016llx  RFLAGS %016llx", (unsigned long long)r->r15,
      (unsigned long long)rflags);
}

static bool panic_is_kernel_addr(uint64_t a)
{
  return a >= 0xffff800000000000ULL;
}

static void panic_stack_dump(uint64_t rsp)
{
  section("Stack");
  if (!panic_is_kernel_addr(rsp))
  {
    out(PSCR_DIM, "RSP is not in the kernel half, skipped");
    return;
  }
  volatile uint64_t *sp = (volatile uint64_t *)(uintptr_t)rsp;
  for (int i = 0; i < 24; i += 3)
    out(PSCR_DIM, "+%03x  %016llx %016llx %016llx", (unsigned int)(i * 8),
        (unsigned long long)sp[i], (unsigned long long)sp[i + 1],
        (unsigned long long)sp[i + 2]);
}

static void panic_call_trace(uint64_t rbp, uint64_t rsp)
{
  section("Call trace");
  int printed = 0;
  for (int depth = 0; depth < 24 && panic_is_kernel_addr(rbp); depth++)
  {
    volatile uint64_t *frame = (volatile uint64_t *)(uintptr_t)rbp;
    uint64_t next_rbp = frame[0];
    uint64_t ret_addr = frame[1];
    if (ret_addr >= 0xffffffff80000000ULL)
    {
      const char *name = 0;
      uint64_t off = 0;
      if (ksym_resolve(ret_addr, &name, &off))
        out(PSCR_TEXT, "#%-2u %s+0x%llx", (unsigned int)printed, name,
            (unsigned long long)off);
      else
        out(PSCR_TEXT, "#%-2u %016llx ??", (unsigned int)printed,
            (unsigned long long)ret_addr);
      printed++;
    }
    if (next_rbp == 0 || next_rbp <= rbp || !panic_is_kernel_addr(next_rbp))
      break;
    rbp = next_rbp;
  }
  if (printed == 0)
    out(PSCR_DIM, "no kernel return addresses found");
  panic_stack_dump(rsp);
}

static void panic_footer(void)
{
  klog(KLOG_ERROR, "========================CPU HALTED=======================");
  klog(KLOG_ERROR, "https://github.com/PureC-OS/PureC-OS/issues/new");
  char line[192];
  kformat(line, sizeof(line),
          "CPU halted  |  build %s  |  scan the QR code or report at github.com/PureC-OS/PureC-OS/issues",
          kernel_build_uuid);
  pscr_footer(line);
}

static void fault_details(uint64_t vector, uint64_t error_code, uint64_t cs,
                          uint64_t cr2)
{
  section("Fault");
  out(PSCR_TEXT, "vector %u  error code %016llx  CS %04llx", (unsigned int)vector,
      (unsigned long long)error_code, (unsigned long long)cs);
  if (vector == 14)
  {
    out(PSCR_ACCENT, "address %016llx", (unsigned long long)cr2);
    out(PSCR_TEXT, "%s %s of a %s page%s%s",
        (error_code & 4) ? "user" : "kernel",
        (error_code & 16) ? "instruction fetch" : (error_code & 2) ? "write" : "read",
        (error_code & 1) ? "present (protection)" : "not-present",
        (error_code & 8) ? ", reserved bit set" : "",
        (error_code & 32) ? ", protection key" : "");
  }
  else if ((vector >= 10 && vector <= 13) && error_code)
  {
    out(PSCR_TEXT, "selector index %u in %s%s", (unsigned int)((error_code >> 3) & 0x1FFF),
        (error_code & 2) ? "IDT" : (error_code & 4) ? "LDT" : "GDT",
        (error_code & 1) ? ", external event" : "");
  }
  else if (vector == 21)
  {
    static const char *const cp[] = {"?", "near ret", "far ret/iret", "endbranch",
                                     "rstorssp", "setssbsy"};
    out(PSCR_TEXT, "reason: %s", cp[(error_code & 0x7FFF) <= 5 ? (error_code & 0x7FFF) : 0]);
  }
}

void kernel_panic_at(const char *file, int line, const char *func,
                     const char *reason)
{
  panic_header(PANIC_KIND_SOFTWARE, reason, file, line, func);
  uint64_t rbp = arch_get_fp();
  uint64_t rsp = arch_get_sp();
  panic_cpu_state();
  panic_call_trace(rbp, rsp);
  panic_qr_show(reason, file, line, func,
                (uint64_t)__builtin_return_address(0), rbp);
  panic_footer();
  panic_halt();
}

void kernel_panic_exception_at(const char *file, int line, const char *func,
                               uint64_t vector, uint64_t error_code,
                               uint64_t rip, uint64_t cs, uint64_t rflags,
                               uint64_t cr2,
                               const struct panic_registers *regs)
{
  char title[64];
  kformat(title, sizeof(title), "exception: #%u %s", (unsigned int)vector,
          exception_name(vector));
  panic_header(vector < 256 ? (int64_t)vector : 255, title, file, line, func);
  fault_details(vector, error_code, cs, cr2);
  panic_rip_sym(rip);
  panic_cpu_state();
  uint64_t rsp = 0;
  uint64_t rbp = 0;
  if (regs)
  {
    rsp = (uint64_t)(uintptr_t)regs;
    rbp = regs->rbp;
    panic_gp_regs(regs, rsp, rflags);
  }
  panic_call_trace(rbp, rsp);
  panic_qr_show(title, file, line, func, rip, rbp);
  panic_footer();
  panic_halt();
}

void kernel_panic_manual_at(const char *file, int line, const char *func,
                            const char *reason, uint64_t rip, uint64_t rsp,
                            uint64_t rbp, const struct panic_registers *regs)
{
  panic_header(PANIC_KIND_MANUAL, reason, file, line, func);
  panic_rip_sym(rip);
  panic_cpu_state();
  if (regs)
    panic_gp_regs(regs, rsp, 0);
  panic_call_trace(rbp, rsp);
  panic_qr_show(reason, file, line, func, rip, rbp);
  panic_footer();
  panic_halt();
}
