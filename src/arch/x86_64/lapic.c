#include <arch/arch.h>
#include <arch/x86_64/lapic.h>
#include <arch/x86_64/mmio.h>
#include <drivers/interrupts/timer.h>
#include <kernel/sync/spinlock.h>
#include <kernel/smp/cpu.h>

static volatile uint32_t *registers;
static bool x2apic;
static bool ready;
static uint32_t timer_count;
static uint32_t read_reg(uint32_t offset) {
  return x2apic ? (uint32_t)arch_rdmsr(0x800 + offset / 16) : registers[offset / 4];
}
static void write_reg(uint32_t offset, uint32_t value) {
  if (x2apic)
    arch_wrmsr(0x800 + offset / 16, value);
  else {
    registers[offset / 4] = value;
    (void)registers[0x20 / 4];
  }
}

bool lapic_init_cpu(void) {
  if (!ready)
    return false;
  uint64_t base = arch_rdmsr(0x1B);
  if (((base >> 10) & 1) != x2apic)
    return false;
  arch_wrmsr(0x1B, base | (1ULL << 11));
  write_reg(0x80, 0);
  write_reg(0xF0, (1U << 8) | SMP_SPURIOUS_VECTOR);
  write_reg(0x320, (1U << 16) | SMP_TIMER_VECTOR);
  if (!(base & (1ULL << 8))) {
    write_reg(0x350, 1U << 16);
    write_reg(0x360, 1U << 16);
  }
  write_reg(0x3E0, 3);
  return true;
}

bool lapic_init(void) {
  uint32_t a, b, c, d;
  arch_cpuid(1, 0, &a, &b, &c, &d);
  if (!(d & (1U << 9)))
    return false;
  uint64_t base = arch_rdmsr(0x1B);
  x2apic = (base & (1ULL << 10)) != 0;
  if (!x2apic) {
    registers = (volatile uint32_t *)mmio_map(base & 0xFFFFF000ULL, 4096);
    if (!registers)
      return false;
  }
  ready = true;
  if (!lapic_init_cpu()) {
    ready = false;
    return false;
  }
  uint64_t flags = irq_save();
  write_reg(0x380, UINT32_MAX);
  uint64_t start = timer_ticks();
  while (timer_ticks() - start < 10)
    arch_cpu_relax();
  uint32_t elapsed = UINT32_MAX - read_reg(0x390);
  write_reg(0x380, 0);
  timer_count = elapsed / 2;
  irq_restore(flags);
  if (!timer_count) {
    ready = false;
    return false;
  }
  return true;
}

bool lapic_start_timer(void) {
  if (!ready)
    return false;
  write_reg(0x320, (1U << 17) | SMP_TIMER_VECTOR);
  write_reg(0x380, timer_count);
  return true;
}
void lapic_eoi(void) {
  if (ready)
    write_reg(0xB0, 0);
}

bool lapic_send(uint32_t id, uint32_t command) {
  const struct cpu_info *cpu = cpu_get_info(id);
  if (!ready || !cpu || !cpu->ids_valid)
    return false;
  uint64_t flags = irq_save();
  if (x2apic) {
    arch_wrmsr(0x830, ((uint64_t)cpu->lapic_id << 32) | command);
  } else {
    if (cpu->lapic_id > 255) {
      irq_restore(flags);
      return false;
    }
    uint64_t start = timer_ticks();
    while (read_reg(0x300) & (1U << 12)) {
      if (timer_ticks() - start > 100) {
        irq_restore(flags);
        return false;
      }
      arch_cpu_relax();
    }
    write_reg(0x310, cpu->lapic_id << 24);
    write_reg(0x300, command);
  }
  irq_restore(flags);
  return true;
}

bool arch_local_timer_start(void) {
  return lapic_start_timer();
}
