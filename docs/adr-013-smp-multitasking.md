# ADR-013: Incremental SMP bring-up

## Status

In progress. Stage 1 implements CPU discovery via Limine. The scheduler still
runs only on the bootstrap processor (BSP). The previous version of this ADR
described a completed SMP scheduler that is not present in the current code.

## Stage 1: CPU inventory

- `src/kernel/smp/cpu.c` copies processor UIDs and 32-bit LAPIC IDs from the
  Limine response into a kernel-owned table. Logical CPU 0 always represents
  the BSP, even when the BSP is not first in the response.
- Detected CPUs, registered table entries, and CPUs executing kernel code
  (`online`) are separate counts. The table holds up to 16 entries, including
  the BSP; larger systems retain their reported detected count and log the
  registration limit. These are logical processors, including SMT threads.
- Only the BSP is online. Discovery does not write `goto_address` or
  `extra_argument`; APs remain parked by Limine. No bootloader pointers are
  retained in the inventory.
- Missing or invalid CPU lists fall back to one BSP with unknown hardware IDs.
  Direct MADT discovery is a later fallback; this stage uses Limine only.
- `SYS_CPU_INFO` gets its logical processor count from this inventory instead
  of the CPUID leaf 1 package limit. Scheduler capacity and affinity remain
  limited to one CPU.

The handoff uses the existing structures in `src/boot/limine.h`, following the
[Limine multiprocessor protocol](https://github.com/limine-bootloader/limine/blob/v8.x/PROTOCOL.md#mp-multiprocessor-feature).

## Next stages

1. Prepare per-CPU execution state and stacks, GDT/TSS and interrupt handling;
   release one AP via Limine and have it acknowledge startup and enter idle.
   Limine already handles the initial processor bootstrap, so this path does
   not need a second INIT/SIPI implementation in the kernel.
2. Protect PCI CF8/CFC transactions and shared PMM, VMM, process, scheduler and
   VFS state before APs can execute general kernel work. Move `current` and
   idle state to per-CPU storage and make context switches safe across CPUs.
3. Add LAPIC timers, scheduler affinity, migration, reschedule IPIs, real TLB
   shootdown and a way to stop other CPUs during panic. A page-fault retry is
   not sufficient TLB synchronization, especially for unmapping/reusing pages.
4. Complete the remaining Ring-3 migration independently of CPU discovery.

## Validation

`make test-cpu` exercises discovery on the host, including absent/malformed
responses, 1/2/4 CPUs, noncontiguous 32-bit APIC IDs, a BSP beyond the table
limit, and confirmation that discovery leaves AP handoff fields untouched.

After `make iso`, boot QEMU with `-smp 1`, `-smp 2`, and `-smp 4`. For example:

```sh
qemu-system-x86_64 -accel tcg -m 512M -smp 4 \
  -cdrom bin/purec_limine.iso -boot d -display none \
  -serial stdio -monitor none -no-reboot -net none
```

Expect `smp: detected=4 registered=4 online=1 source=Limine`, one BSP and
three parked APs, followed by `sched: active cores=1`. This verifies discovery
and continued BSP boot; AP execution remains a separate milestone.
