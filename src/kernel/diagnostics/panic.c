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
#include "../../mm/pmm.h"
#include "../../mm/vmm.h"
#include "../core/system_info.h"
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

static void hex_upper(char *s)
{
  for (; *s; s++)
  {
    if (s[0] != '0' || s[1] != 'x')
      continue;
    s += 2;
    while ((*s >= '0' && *s <= '9') || (*s >= 'a' && *s <= 'f') || (*s >= 'A' && *s <= 'F'))
    {
      if (*s >= 'a' && *s <= 'f')
        *s = (char)(*s - 'a' + 'A');
      s++;
    }
    s--;
  }
}

static void out(const char *fmt, ...)
{
  char line[192];
  va_list ap;
  va_start(ap, fmt);
  kvformat(line, sizeof(line), fmt, ap);
  va_end(ap);
  hex_upper(line);
  klog(KLOG_ERROR, line);
  pscr_line(line);
}

#define SEP "  =================================================="

static void panic_header(int64_t kind, const char *reason, const char *file,
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
  pscr_begin(theme);
  const char *stop = theme->code[0] == '#' ? theme->code + 1 : theme->code;
  out("  :(   PureC OS KERNEL PANIC");
  out(SEP);
  if (kind >= 0)
    out("  Reason: %s %s", theme->name, theme->code);
  else
    out("  Reason: %s", reason ? reason : "unknown fatal error");
  out("  Stop code: %s", stop);
  out("  At: %s:%d %s()", file ? file : "?", line, func ? func : "?");
  out("  Build: %s %s", kernel_build_uuid, kernel_build_time);
  out("  Boot stage: %02u %s", (unsigned int)boot_diag_current_stage(),
      boot_diag_current_detail());
  out(SEP);
}

static void flags_text(uint64_t rflags, char *buf, uint32_t cap)
{
  static const struct
  {
    uint8_t bit;
    const char *name;
  } names[] = {{0, "CF"}, {2, "PF"}, {4, "AF"}, {6, "ZF"}, {7, "SF"}, {8, "TF"},
               {9, "IF"}, {10, "DF"}, {11, "OF"}, {14, "NT"}, {16, "RF"},
               {17, "VM"}, {18, "AC"}, {21, "ID"}};
  uint32_t n = 0;
  for (uint32_t i = 0; i < sizeof(names) / sizeof(names[0]) && n + 4 < cap; i++)
    if (rflags & (1ULL << names[i].bit))
    {
      buf[n++] = names[i].name[0];
      buf[n++] = names[i].name[1];
      buf[n++] = ' ';
    }
  buf[n] = '\0';
}

static void panic_regs(const struct panic_registers *r, uint64_t rip, uint64_t cs,
                       uint64_t rsp, uint64_t ss, uint64_t rflags)
{
  out("REGS:");
  if (r)
  {
    out("  RAX=0x%016llx  RBX=0x%016llx", (unsigned long long)r->rax, (unsigned long long)r->rbx);
    out("  RCX=0x%016llx  RDX=0x%016llx", (unsigned long long)r->rcx, (unsigned long long)r->rdx);
    out("  RSI=0x%016llx  RDI=0x%016llx", (unsigned long long)r->rsi, (unsigned long long)r->rdi);
    out("  RBP=0x%016llx  R08=0x%016llx", (unsigned long long)r->rbp, (unsigned long long)r->r8);
    out("  R09=0x%016llx  R10=0x%016llx", (unsigned long long)r->r9, (unsigned long long)r->r10);
    out("  R11=0x%016llx  R12=0x%016llx", (unsigned long long)r->r11, (unsigned long long)r->r12);
    out("  R13=0x%016llx  R14=0x%016llx", (unsigned long long)r->r13, (unsigned long long)r->r14);
    out("  R15=0x%016llx", (unsigned long long)r->r15);
  }
  out("  RSP=0x%016llx  SS=0x%016llx", (unsigned long long)rsp, (unsigned long long)ss);
  out("  RIP=0x%016llx  CS=0x%016llx", (unsigned long long)rip, (unsigned long long)cs);
  char flags[64];
  flags_text(rflags, flags, sizeof(flags));
  out("  RFLAGS=0x%016llx [%s] IOPL=%u", (unsigned long long)rflags, flags,
      (unsigned int)((rflags >> 12) & 3));
}

static void panic_fault(uint64_t vector, uint64_t err, uint64_t cr2)
{
  out("VEC=%u ERR=0x%016llx", (unsigned int)vector, (unsigned long long)err);
  if (vector == 14)
  {
    out("#PF: %s %s %s%s%s", (err & 1) ? "protection" : "not-present",
        (err & 16) ? "ifetch" : (err & 2) ? "write" : "read",
        (err & 4) ? "user" : "super", (err & 8) ? " reserved-bit" : "",
        (err & 32) ? " pkey" : "");
    out("  CR2=0x%016llx", (unsigned long long)cr2);
  }
  else if (vector == 8 || (vector >= 10 && vector <= 13) || vector == 17 || vector == 21)
  {
    out("  EXT=%u IDT=%u TI=%u idx=0x%016llx", (unsigned int)(err & 1),
        (unsigned int)((err >> 1) & 1), (unsigned int)((err >> 2) & 1),
        (unsigned long long)((err >> 3) & 0x1FFF));
  }
}

static void panic_cpu(void)
{
  uint32_t a = 0, b = 0, c = 0, d = 0;
  arch_cpuid(0, 0, &a, &b, &c, &d);
  char vendor[13];
  for (int i = 0; i < 4; i++)
  {
    vendor[i] = (char)(b >> (8 * i));
    vendor[4 + i] = (char)(d >> (8 * i));
    vendor[8 + i] = (char)(c >> (8 * i));
  }
  vendor[12] = '\0';
  arch_cpuid(1, 0, &a, &b, &c, &d);
  struct thread *t = scheduler_current_thread();
  out("CPU: %s APIC=%u  TID=%d", vendor, (unsigned int)(b >> 24),
      t ? (int)t->id : -1);
  out("CR0=0x%016llx CR2=0x%016llx", (unsigned long long)arch_read_cr0(),
      (unsigned long long)arch_read_cr2());
  out("CR3=0x%016llx CR4=0x%016llx", (unsigned long long)arch_read_cr3(),
      (unsigned long long)arch_read_cr4());
  out("EFER=0x%016llx", (unsigned long long)arch_rdmsr(0xC0000080));
  out("BRAND: %s", system_info_cpu_name());
}

static bool panic_is_kernel_addr(uint64_t a)
{
  return a >= 0xffff800000000000ULL;
}

static void panic_trace(uint64_t rbp)
{
  out("");
  out("KERNEL STACK TRACE:");
  int printed = 0;
  for (int depth = 0; depth < 20 && panic_is_kernel_addr(rbp); depth++)
  {
    volatile uint64_t *frame = (volatile uint64_t *)(uintptr_t)rbp;
    uint64_t next = frame[0];
    uint64_t ret = frame[1];
    if (ret >= 0xffffffff80000000ULL)
    {
      const char *name = 0;
      uint64_t off = 0;
      if (ksym_resolve(ret, &name, &off))
        out("  [%d] 0x%016llx  %s+0x%llx", printed, (unsigned long long)ret, name,
            (unsigned long long)off);
      else
        out("  [%d] 0x%016llx  ??", printed, (unsigned long long)ret);
      printed++;
    }
    if (next == 0 || next <= rbp || !panic_is_kernel_addr(next))
      break;
    rbp = next;
  }
  if (!printed)
    out("  (no kernel frames)");
}

static void translate(const char *label, uint64_t va)
{
  uint64_t pa = vmm_translate(arch_read_cr3(), va);
  if (pa)
    out("%s 0x%016llx -> 0x%016llx", label, (unsigned long long)va, (unsigned long long)pa);
  else
    out("%s 0x%016llx -> unmapped or unavailable", label, (unsigned long long)va);
}

static void panic_memory(uint64_t rip, bool have_cr2, uint64_t cr2, uint64_t rsp)
{
  out("");
  out("KERNEL MEMORY: free pages=%llu", (unsigned long long)(pmm_free_bytes() / 4096));
  translate("RIP", rip);
  if (have_cr2)
    translate("CR2", cr2);
  if (!panic_is_kernel_addr(rsp))
  {
    out("STACK @0x%016llx: not in the kernel half", (unsigned long long)rsp);
    return;
  }
  out("STACK @0x%016llx:", (unsigned long long)rsp);
  volatile uint64_t *sp = (volatile uint64_t *)(uintptr_t)rsp;
  for (int i = 0; i < 8; i++)
    out("  0x%016llx: 0x%016llx", (unsigned long long)(rsp + (uint64_t)i * 8),
        (unsigned long long)sp[i]);
}

static void panic_footer(void)
{
  out("");
  out("System halted. Please reboot.");
  if (pscr_active())
    klog(KLOG_ERROR, "Scan the QR code to open a prefilled GitHub issue");
}

static uint64_t read_ss(void)
{
  uint64_t ss;
  __asm__ volatile("mov %%ss, %0" : "=r"(ss));
  return ss;
}

static uint64_t read_cs(void)
{
  uint64_t cs;
  __asm__ volatile("mov %%cs, %0" : "=r"(cs));
  return cs;
}

void kernel_panic_at(const char *file, int line, const char *func,
                     const char *reason)
{
  panic_header(PANIC_KIND_SOFTWARE, reason, file, line, func);
  uint64_t rbp = arch_get_fp();
  uint64_t rsp = arch_get_sp();
  uint64_t rip = (uint64_t)__builtin_return_address(0);
  struct panic_registers regs;
  ARCH_CAPTURE_PANIC_REGS(regs);
  regs.rbp = rbp;
  panic_regs(&regs, rip, read_cs(), rsp, read_ss(), arch_irq_save());
  panic_cpu();
  panic_trace(rbp);
  panic_memory(rip, false, 0, rsp);
  panic_qr_show(reason, file, line, func, rip, rbp);
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
  panic_header(vector < 32 ? (int64_t)vector : 255, title, file, line, func);
  uint64_t rsp = 0, ss = 0, rbp = 0;
  if (regs)
  {
    const uint64_t *frame = (const uint64_t *)regs;
    rsp = frame[20];
    ss = frame[21];
    rbp = regs->rbp;
  }
  panic_regs(regs, rip, cs, rsp, ss, rflags);
  panic_fault(vector, error_code, cr2);
  panic_cpu();
  if ((cs & 3) == 3)
    out("User RIP above; kernel trace starts in panic handler.");
  panic_trace((cs & 3) == 3 ? arch_get_fp() : rbp);
  panic_memory(rip, vector == 14, cr2, (cs & 3) == 3 ? (uint64_t)(uintptr_t)regs : rsp);
  panic_qr_show(title, file, line, func, rip, rbp);
  panic_footer();
  panic_halt();
}

void kernel_panic_manual_at(const char *file, int line, const char *func,
                            const char *reason, uint64_t rip, uint64_t rsp,
                            uint64_t rbp, const struct panic_registers *regs)
{
  panic_header(PANIC_KIND_MANUAL, reason, file, line, func);
  panic_regs(regs, rip, read_cs(), rsp, read_ss(), arch_irq_save());
  panic_cpu();
  panic_trace(rbp);
  panic_memory(rip, false, 0, rsp);
  panic_qr_show(reason, file, line, func, rip, rbp);
  panic_footer();
  panic_halt();
}
