# ADR-013: Incremental SMP bring-up

## Status

In progress. Stage 1 implements CPU discovery via Limine. Stage 2 releases one
application processor (AP) into an isolated idle loop. The scheduler still runs
only on the bootstrap processor (BSP). The previous version of this ADR
described a completed SMP scheduler that is not present in the current code.

## Stage 1: CPU inventory

- `src/kernel/smp/cpu.c` copies processor UIDs and 32-bit LAPIC IDs from the
  Limine response into a kernel-owned table. Logical CPU 0 always represents
  the BSP, even when the BSP is not first in the response.
- Detected CPUs, registered table entries, and CPUs executing kernel code
  (`online`) are separate counts. The table holds up to 16 entries, including
  the BSP; larger systems retain their reported detected count and log the
  registration limit. These are logical processors, including SMT threads.
- Discovery does not write `goto_address` or `extra_argument`; APs remain
  parked until the separate bring-up stage. No bootloader pointers are retained
  in the inventory.
- Missing or invalid CPU lists fall back to one BSP with unknown hardware IDs.
  Direct MADT discovery is a later fallback; this stage uses Limine only.
- `SYS_CPU_INFO` gets its logical processor count from this inventory instead
  of the CPUID leaf 1 package limit. Scheduler capacity and affinity remain
  limited to one CPU.

The handoff uses the existing structures in `src/boot/limine.h`, following the
[Limine multiprocessor protocol](https://github.com/limine-bootloader/limine/blob/v8.x/PROTOCOL.md#mp-multiprocessor-feature).

## Stage 2: one isolated AP

- After PMM, VMM and BSP FPU initialization, the BSP releases logical CPU 1 by
  atomically setting its Limine `extra_argument` and `goto_address` fields.
- The assembly entry immediately switches away from the temporary Limine stack
  to a dedicated 32 KiB kernel stack. CPU 1 then installs the kernel CR3 and
  NX state, a private GDT/TSS with private double-fault/NMI/machine-check IST
  stacks, the shared read-only IDT, and its own FPU control state.
- An `int3` round trip verifies that exceptions return on the AP. CPU 1 then
  publishes the atomic `CPU_IDLE` state and halts with interrupts disabled.
- Startup has a one-second timeout. Failure leaves the BSP boot path usable;
  a late AP cannot overwrite the timeout state.
- CPU 1 does not run drivers, kernel threads, userspace or the scheduler. It
  does not touch shared logging, allocator or process state after its startup
  self-test. On systems with more CPUs, CPU 2 and above remain parked.

## Next stages

1. Protect PCI CF8/CFC transactions and shared PMM, VMM, process, scheduler and
   VFS state before APs can execute general kernel work. Move `current` and
   idle state to per-CPU storage and make context switches safe across CPUs.
2. Add LAPIC timers, scheduler affinity, migration, reschedule IPIs, real TLB
   shootdown and a way to stop other CPUs during panic. A page-fault retry is
   not sufficient TLB synchronization, especially for unmapping/reusing pages.
3. Release the remaining registered APs once those shared paths are safe.
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

With `-smp 4`, expect the initial inventory to report one online BSP and three
parked APs, followed by `smp: cpu1 lapic_id=1 idle; online=2` and
`sched: 2 of 4 CPUs online; scheduler remains BSP-only`. CPU 2 and CPU 3 stay
parked. The BSP must still reach `[SCHED] start` without a panic.
