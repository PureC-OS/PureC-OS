# ADR-013: SMP Multitasking

## Status

Accepted. Implemented: LAPIC timer, Limine AP bring-up, SMP scheduler,
spinlock + mutex.

## Design

- CPUs: up to 16, bring-up via Limine SMP `goto_address`. AP trampoline
  loads kernel CR3, per-CPU stack, then `ap_main`: per-CPU GDT+TSS,
  IDT reload, FPU, GS.base, LAPIC timer. Failed APs fall back to
  fewer CPUs, never panic.
- Time: TSC milliseconds are global. PIT IRQ0 stays only for the
  single-CPU fallback; with SMP each core runs a periodic LAPIC
  timer (1 ms, vector 0xE0) and the PIT line is masked.
- PIC stays for legacy IRQs (keyboard/mouse on BSP). No IOAPIC yet.
- Scheduler: one global thread table (64 threads) under a spinlock,
  per-CPU `current`/`idle` via GS.base. Selection is priority with
  aging (`waited - priority*64`), affinity honored, each CPU has
  its own idle thread. No lock is held across context switches.
- No IPIs: unblocked threads are picked up by the next local tick
  (<=1 ms). TLB staleness on other CPUs is absorbed by the #PF
  handler: present mapping -> invlpg + resume, real fault -> kill.
- Primitives: `spinlock_t` (irqsave), blocking `mutex` with FIFO
  handoff, no priority inheritance.

## Limits

64 threads, 16 CPUs, no IOAPIC/X2APIC/IPI, no userspace mutex API yet.
