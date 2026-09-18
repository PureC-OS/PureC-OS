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

## Stage 3a: CPU-local scheduler state

- Each CPU owns its current/idle pointers, initialization/start flags,
  reschedule flag and tick counters in a cache-line-aligned scheduler slot.
  Only the BSP initializes the shared thread table; AP slots remain inactive.
- The current logical CPU is identified through its private kernel GDTR.
  This works across Ring-3 transitions without introducing GS/swapgs handling.
  It requires the kernel GDT to be installed before scheduler access.
- Kernel stack updates now target the executing CPU's TSS. AP startup checks
  its CPU identity and verifies that it has no current scheduler thread.
- Selection returns the local idle pointer without prematurely changing its
  state. The shared runnable table and affinity remain BSP-only.
- CPU pointers saved in suspended scheduler frames assume no migration.
  Migration must revisit these frames before threads can move between CPUs.
- Tick counter accessors describe the executing CPU; all current callers run
  on the BSP. System-wide aggregation is deferred to AP scheduling.

## Next stages

1. Protect PCI CF8/CFC transactions and shared PMM, VMM, process, scheduler and
   VFS state before APs can execute general kernel work. Make context switches
   and shared runnable-thread selection safe across CPUs.
2. Add LAPIC timers, scheduler affinity, migration, reschedule IPIs, real TLB
   shootdown and a way to stop other CPUs during panic. A page-fault retry is
   not sufficient TLB synchronization, especially for unmapping/reusing pages.
3. Release the remaining registered APs once those shared paths are safe.
4. Complete the remaining Ring-3 migration independently of CPU discovery.

## Validation

`make test-cpu` exercises discovery on the host, including absent/malformed
responses, 1/2/4 CPUs, noncontiguous 32-bit APIC IDs, a BSP beyond the table
limit, and confirmation that discovery leaves AP handoff fields untouched.

`make test-scheduler-cpu` checks CPU-local current pointers, flags, counters,
round-robin selection, and local idle fallback using a mocked CPU identity.

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

Stage 3a smoke validation: QEMU TCG with 1, 2 and 4 CPUs reached
`[SCHED] start` and started init/login/input/keyboard/log/network threads without
logged panics. The test ISO used the built kernel and a 640x480 Limine mode to
reduce framebuffer logging overhead. CPU 1 reached isolated idle in the 2/4-CPU
runs; this validates bring-up and BSP scheduling, not concurrent AP scheduling.
