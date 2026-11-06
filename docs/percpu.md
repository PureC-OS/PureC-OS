# Per-CPU area and swapgs

Every CPU has one `struct percpu` (`src/kernel/smp/percpu.h`):

| offset | field    | meaning                                  |
|--------|----------|------------------------------------------|
| 0      | `self`   | pointer to this structure                |
| 8      | `cpu_id` | logical CPU id                           |
| 16     | `current`| thread running on this CPU               |

`percpu_cpu_id()` and `percpu_current()` read the fields with a single
`mov %gs:offset`. A single instruction cannot be split by an interrupt, so the
result always belongs to the CPU and thread that executed it. Reading the CPU
id first and then indexing a per-CPU array is not safe with interrupts enabled
because the thread can migrate in between.

## GS base convention

- In kernel mode `IA32_GS_BASE` holds the per-CPU pointer and
  `IA32_KERNEL_GS_BASE` holds the user value (always 0, user space does not use
  GS).
- In user mode the two are swapped.

## Entry and exit

All interrupts, exceptions and `int 0x80` enter through `isr_common`
(`src/arch/x86_64/idt/asm/idt.asm`).

- Vectors other than 2 (NMI), 8 (`#DF`) and 18 (`#MC`): `swapgs` on entry when
  the saved `CS` has RPL 3, and again before `iretq` when it was done on entry.
  The decision is kept in `r12`, which is callee-saved across `isr_handler` and
  across `scheduler_asm_switch`.
- NMI, `#DF`, `#MC` can arrive at any point, including between the hardware
  entry and `swapgs`. They read `IA32_GS_BASE` and swap only when it is not a
  kernel address (high bit clear). The result is undone on exit the same way.
- `arch_enter_user` and `arch_enter_user_frame` (`src/arch/x86_64/user.asm`)
  execute `swapgs` before loading user segment registers and `iretq`.

## Initialisation

`gdt_init_cpu()` installs the per-CPU area for the CPU after `gdt_flush`.
Application processors also install it first thing in `smp_ap_main()`.
`gdt_flush` no longer reloads `FS` and `GS`: loading a segment selector sets the
segment base from the descriptor and would clear `IA32_GS_BASE`.

## Rules

- Per-CPU data owned by a CPU is written only by that CPU, with interrupts
  disabled, or published with an atomic store.
- Anything that needs "this CPU" with a stable meaning must run with interrupts
  or preemption disabled, or use the `%gs` accessors.
